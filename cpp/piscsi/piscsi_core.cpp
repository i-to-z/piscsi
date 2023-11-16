//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Powered by XM6 TypeG Technology.
// Copyright (C) 2016-2020 GIMONS
// Copyright (C) 2020-2023 Contributors to the PiSCSI project
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "shared/config.h"
#include "shared/piscsi_util.h"
#include "shared/protobuf_util.h"
#include "shared/piscsi_exceptions.h"
#include "shared/piscsi_version.h"
#include "controllers/scsi_controller.h"
#include "devices/device_logger.h"
#include "devices/storage_device.h"
#include "devices/host_services.h"
#include "hal/gpiobus_factory.h"
#include "hal/gpiobus.h"
#include "piscsi/piscsi_core.h"
#include <spdlog/spdlog.h>
#include <netinet/in.h>
#include <csignal>
#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>

using namespace std;
using namespace filesystem;
using namespace spdlog;
using namespace piscsi_interface;
using namespace piscsi_util;
using namespace protobuf_util;
using namespace scsi_defs;

void Piscsi::Banner(span<char *> args) const
{
	cout << piscsi_util::Banner("(Backend Service)");
	cout << "Connection type: " << CONNECT_DESC << '\n' << flush;

	if ((args.size() > 1 && strcmp(args[1], "-h") == 0) || (args.size() > 1 && strcmp(args[1], "--help") == 0)){
		cout << "\nUsage: " << args[0] << " [-idID[:LUN] FILE] ...\n\n";
		cout << " ID is SCSI device ID (0-" << (ControllerManager::GetScsiIdMax() - 1) << ").\n";
		cout << " LUN is the optional logical unit (0-" << (ControllerManager::GetScsiLunMax() - 1) <<").\n";
		cout << " FILE is a disk image file, \"daynaport\", \"bridge\", \"printer\" or \"services\".\n\n";
		cout << " Image type is detected based on file extension if no explicit type is specified.\n";
		cout << "  hd1 : SCSI-1 HD image (Non-removable generic SCSI-1 HD image)\n";
		cout << "  hds : SCSI HD image (Non-removable generic SCSI HD image)\n";
		cout << "  hdr : SCSI HD image (Removable generic HD image)\n";
		cout << "  hda : SCSI HD image (Apple compatible image)\n";
		cout << "  hdn : SCSI HD image (NEC compatible image)\n";
		cout << "  hdi : SCSI HD image (Anex86 HD image)\n";
		cout << "  nhd : SCSI HD image (T98Next HD image)\n";
		cout << "  mos : SCSI MO image (MO image)\n";
		cout << "  iso : SCSI CD image (ISO 9660 image)\n";
		cout << "  is1 : SCSI CD image (ISO 9660 image, SCSI-1)\n" << flush;

		exit(EXIT_SUCCESS);
	}
}

bool Piscsi::InitBus()
{
	bus = GPIOBUS_Factory::Create(BUS::mode_e::TARGET);
	if (bus == nullptr) {
		return false;
	}

	controller_manager = make_shared<ControllerManager>();

	executor = make_unique<PiscsiExecutor>(*bus, controller_manager);

	dispatcher = make_shared<CommandDispatcher>(piscsi_image, response, *executor);

	return true;
}

void Piscsi::CleanUp()
{
	if (service.IsRunning()) {
		service.Stop();
	}

	executor->DetachAll();

	// TODO Check why there are rare cases where bus is NULL on a remote interface shutdown
	// even though it is never set to NULL anywhere
	if (bus) {
		bus->Cleanup();
	}
}

void Piscsi::ReadAccessToken(const path& filename)
{
	if (error_code error; !is_regular_file(filename, error)) {
		throw parser_exception("Access token file '" + filename.string() + "' must be a regular file");
	}

	if (struct stat st; stat(filename.c_str(), &st) || st.st_uid || st.st_gid) {
		throw parser_exception("Access token file '" + filename.string() + "' must be owned by root");
	}

	if (const auto perms = filesystem::status(filename).permissions();
		(perms & perms::group_read) != perms::none || (perms & perms::others_read) != perms::none ||
			(perms & perms::group_write) != perms::none || (perms & perms::others_write) != perms::none) {
		throw parser_exception("Access token file '" + filename.string() + "' must be readable by root only");
	}

	ifstream token_file(filename);
	if (token_file.fail()) {
		throw parser_exception("Can't open access token file '" + filename.string() + "'");
	}

	getline(token_file, access_token);
	if (token_file.fail()) {
		throw parser_exception("Can't read access token file '" + filename.string() + "'");
	}

	if (access_token.empty()) {
		throw parser_exception("Access token file '" + filename.string() + "' must not be empty");
	}
}

void Piscsi::LogDevices(string_view devices) const
{
	stringstream ss(devices.data());
	string line;

	while (getline(ss, line, '\n')) {
		spdlog::info(line);
	}
}

void Piscsi::TerminationHandler(int)
{
	instance->CleanUp();

	// Process will terminate automatically
}

string Piscsi::ParseArguments(span<char *> args, PbCommand& command, int& port, string& reserved_ids)
{
	string log_level = "info";
	PbDeviceType type = UNDEFINED;
	int block_size = 0;
	string name;
	string id_and_lun;

	string locale = GetLocale();

	// Avoid duplicate messages while parsing
	set_level(level::off);

	opterr = 1;
	int opt;
	while ((opt = getopt(static_cast<int>(args.size()), args.data(), "-Iib:d:n:p:r:t:z:D:F:L:P:R:C:v")) != -1) {
		switch (opt) {
			// The two options below are kind of a compound option with two letters
			case 'i':
			case 'I':
				continue;

			case 'd':
			case 'D':
				id_and_lun = optarg;
				continue;

			case 'b':
				if (!GetAsUnsignedInt(optarg, block_size)) {
					throw parser_exception("Invalid block size " + string(optarg));
				}
				continue;

			case 'z':
				locale = optarg;
				continue;

			case 'F':
				if (const string error = piscsi_image.SetDefaultFolder(optarg); !error.empty()) {
					throw parser_exception(error);
				}
				continue;

			case 'L':
				log_level = optarg;
				continue;

			case 'R':
				int depth;
				if (!GetAsUnsignedInt(optarg, depth)) {
					throw parser_exception("Invalid image file scan depth " + string(optarg));
				}
				piscsi_image.SetDepth(depth);
				continue;

			case 'n':
				name = optarg;
				continue;

			case 'p':
				if (!GetAsUnsignedInt(optarg, port) || port <= 0 || port > 65535) {
					throw parser_exception("Invalid port " + string(optarg) + ", port must be between 1 and 65535");
				}
				continue;

			case 'P':
				ReadAccessToken(optarg);
				continue;

			case 'r':
				reserved_ids = optarg;
				continue;

			case 't':
				type = ParseDeviceType(optarg);
				continue;

			case 1:
				// Encountered filename
				break;

			default:
				throw parser_exception("Parser error");
		}

		if (optopt) {
			throw parser_exception("Parser error");
		}

		// Set up the device data

		auto device = command.add_devices();

		if (!id_and_lun.empty()) {
			if (const string error = SetIdAndLun(*device, id_and_lun); !error.empty()) {
				throw parser_exception(error);
			}
		}

		device->set_type(type);
		device->set_block_size(block_size);

		ParseParameters(*device, optarg);

		SetProductData(*device, name);

		type = UNDEFINED;
		block_size = 0;
		name = "";
		id_and_lun = "";
	}

	if (!CommandDispatcher::SetLogLevel(log_level)) {
		throw parser_exception("Invalid log level '" + log_level + "'");
	}

	return locale;
}

PbDeviceType Piscsi::ParseDeviceType(const string& value)
{
	string t;
	ranges::transform(value, back_inserter(t), ::toupper);
	if (PbDeviceType type; PbDeviceType_Parse(t, &type)) {
		return type;
	}

	throw parser_exception("Illegal device type '" + value + "'");
}

bool Piscsi::ExecuteWithLock(const CommandContext& context)
{
	scoped_lock<mutex> lock(executor->GetExecutionLocker());
	return executor->ProcessCmd(context);
}

bool Piscsi::HandleDeviceListChange(const CommandContext& context, PbOperation operation) const
{
	// ATTACH and DETACH return the resulting device list
	if (operation == ATTACH || operation == DETACH) {
		// A command with an empty device list is required here in order to return data for all devices
		PbCommand command;
		PbResult result;
		response.GetDevicesInfo(executor->GetAllDevices(), result, command, piscsi_image.GetDefaultFolder());
		context.WriteResult(result);
		return result.status();
	}

	return true;
}

int Piscsi::run(span<char *> args)
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	Banner(args);

	// The -v option shall result in no other action except displaying the version
	if (ranges::find_if(args, [] (const char *arg) { return !strcasecmp(arg, "-v"); } ) != args.end()) {
		cout << piscsi_get_version_string() << '\n';
		return EXIT_SUCCESS;
	}

	PbCommand command;
	string locale;
	string reserved_ids;
	int port = DEFAULT_PORT;
	try {
		locale = ParseArguments(args, command, port, reserved_ids);
	}
	catch(const parser_exception& e) {
		cerr << "Error: " << e.what() << endl;

		return EXIT_FAILURE;
	}

	if (!InitBus()) {
		cerr << "Error: Can't initialize bus" << endl;

		return EXIT_FAILURE;
	}

	if (const string error = service.Init([this] (CommandContext& context) {
	    return ExecuteCommand(context);
		}, port); !error.empty()) {
		cerr << "Error: " << error << endl;

		CleanUp();

		return EXIT_FAILURE;
	}

	if (const string error = executor->SetReservedIds(reserved_ids); !error.empty()) {
		cerr << "Error: " << error << endl;

		CleanUp();

		return EXIT_FAILURE;
	}

	if (command.devices_size()) {
		// Attach all specified devices
		command.set_operation(ATTACH);

		if (const CommandContext context(command, piscsi_image.GetDefaultFolder(), locale); !executor->ProcessCmd(context)) {
			cerr << "Error: Can't attach devices" << endl;

			CleanUp();

			return EXIT_FAILURE;
		}

		// Ensure that all host services have a dispatcher
		for (auto device : controller_manager->GetAllDevices()) {
		    if (auto host_services = dynamic_pointer_cast<HostServices>(device); host_services != nullptr) {
		        host_services->SetDispatcher(dispatcher);
		    }
		}
	}

	// Display and log the device list
	PbServerInfo server_info;
	response.GetDevices(executor->GetAllDevices(), server_info, piscsi_image.GetDefaultFolder());
	const vector<PbDevice>& devices = { server_info.devices_info().devices().begin(), server_info.devices_info().devices().end() };
	const string device_list = ListDevices(devices);
	LogDevices(device_list);
	cout << device_list << flush;

	instance = this;
	// Signal handler to detach all devices on a KILL or TERM signal
	struct sigaction termination_handler;
	termination_handler.sa_handler = TerminationHandler;
	sigemptyset(&termination_handler.sa_mask);
	termination_handler.sa_flags = 0;
	sigaction(SIGINT, &termination_handler, nullptr);
	sigaction(SIGTERM, &termination_handler, nullptr);
	signal(SIGPIPE, SIG_IGN);

    // Set the affinity to a specific processor core
	FixCpu(3);

	service.Start();

	Process();

	return EXIT_SUCCESS;
}

void Piscsi::Process()
{
#ifdef USE_SEL_EVENT_ENABLE
	// Scheduling policy setting (highest priority)
	// TODO Check whether this results in any performance gain
	sched_param schparam;
	schparam.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(0, SCHED_FIFO, &schparam);
#else
	cout << "Note: No PiSCSI hardware support, only client interface calls are supported" << endl;
#endif

	// Main Loop
	while (service.IsRunning()) {
#ifdef USE_SEL_EVENT_ENABLE
		// SEL signal polling
		if (!bus->PollSelectEvent()) {
			// Stop on interrupt
			if (errno == EINTR) {
				break;
			}
			continue;
		}

		// Get the bus
		bus->Acquire();
#else
		bus->Acquire();
		if (!bus->GetSEL()) {
			const timespec ts = { .tv_sec = 0, .tv_nsec = 0};
			nanosleep(&ts, nullptr);
			continue;
		}
#endif

		// Only process the SCSI command if the bus is not busy and no other device responded
		if (IsNotBusy() && bus->GetSEL()) {
			scoped_lock<mutex> lock(executor->GetExecutionLocker());

			// Process command on the responsible controller based on the current initiator and target ID
			if (const auto shutdown_mode = controller_manager->ProcessOnController(bus->GetDAT());
			    shutdown_mode != AbstractController::piscsi_shutdown_mode::NONE &&
			    dispatcher->ShutDown(shutdown_mode)) {
			    // When the bus is free PiSCSI or the Pi may be shut down.
			    CleanUp();
			}
		}
	}
}

bool Piscsi::ExecuteCommand(CommandContext& context)
{
    if (!access_token.empty() && access_token != GetParam(context.GetCommand(), "token")) {
        return context.ReturnLocalizedError(LocalizationKey::ERROR_AUTHENTICATION, UNAUTHORIZED);
    }

    context.SetDefaultFolder(piscsi_image.GetDefaultFolder());
    PbResult result;
    const bool status = dispatcher->DispatchCommand(context, result);
    if (status && context.GetCommand().operation() == PbOperation::SHUT_DOWN) {
        CleanUp();
        return false;
    }

    return status;
}

bool Piscsi::IsNotBusy() const
{
    // Wait until BSY is released as there is a possibility for the
    // initiator to assert it while setting the ID (for up to 3 seconds)
    if (bus->GetBSY()) {
        const auto now = chrono::steady_clock::now();
        while ((chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - now).count()) < 3) {
            bus->Acquire();

            if (!bus->GetBSY()) {
                return true;
            }
        }

        return false;
    }

    return true;
}
