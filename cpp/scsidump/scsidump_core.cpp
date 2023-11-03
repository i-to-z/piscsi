//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022 akuker
// Copyright (C) 2022-2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "scsidump/scsidump_core.h"
#include "scsidump/phase_executor.h"
#include "hal/gpiobus_factory.h"
#include "controllers/controller_manager.h"
#include "shared/piscsi_exceptions.h"
#include "shared/piscsi_util.h"
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <array>

using namespace std;
using namespace filesystem;
using namespace spdlog;
using namespace scsi_defs;
using namespace piscsi_util;

void ScsiDump::CleanUp()
{
    if (bus != nullptr) {
        bus->Cleanup();
    }
}

void ScsiDump::TerminationHandler(int)
{
    CleanUp();

	// Process will terminate automatically
}

bool ScsiDump::Banner(ostream& console, span<char *> args) const
{
    console << piscsi_util::Banner("(Hard Disk Dump/Restore Utility)");

    if (args.size() < 2 || string(args[1]) == "-h" || string(args[1]) == "--help") {
    	console << "Usage: " << args[0] << " -t ID[:LUN] [-i BID] [-f FILE] [-a] [-v] [-V] [-r] [-b BUFFER_SIZE]"
        		<< " [-p] [-I] [-s]\n"
				<< " ID is the target device ID (0-" << (ControllerManager::GetScsiIdMax() - 1) << ").\n"
				<< " LUN is the optional target device LUN (0-" << (ControllerManager::GetScsiLunMax() -1 ) << ")."
				<< " Default is 0.\n"
				<< " BID is the PiSCSI board ID (0-7). Default is 7.\n"
				<< " FILE is the image file path. Only needed when not dumping to stdout and no property file is"
				<< " requested.\n"
				<< " BUFFER_SIZE is the transfer buffer size in bytes, at least " << MINIMUM_BUFFER_SIZE
				<< " bytes. Default is 1 MiB.\n"
				<< " -a Scan all potential LUNs during bus scan, default is LUN 0 only.\n"
				<< " -v Enable verbose logging.\n"
				<< " -V Enable even more verbose logging.\n"
				<< " -r Restore instead of dump.\n"
				<< " -p Generate .properties file to be used with the PiSCSI web interface."
				<< " Only valid for dump and inquiry mode.\n"
				<< " -I Display INQUIRY data of ID[:LUN].\n"
				<< " -s Scan SCSI bus for devices.\n"
				<< flush;

        return false;
    }

    return true;
}

bool ScsiDump::Init() const
{
	// Signal handler for cleaning up
	struct sigaction termination_handler;
	termination_handler.sa_handler = TerminationHandler;
	sigemptyset(&termination_handler.sa_mask);
	termination_handler.sa_flags = 0;
	sigaction(SIGTERM, &termination_handler, nullptr);
	signal(SIGPIPE, SIG_IGN);

    bus = GPIOBUS_Factory::Create(BUS::mode_e::INITIATOR);

    return bus != nullptr;
}

void ScsiDump::ParseArguments(span<char *> args)
{
    int buffer_size = DEFAULT_BUFFER_SIZE;

    int opt;
    opterr = 0;
    while ((opt = getopt(static_cast<int>(args.size()), args.data(), "i:f:b:t:arsvpIV")) != -1) {
        switch (opt) {
        case 'i':
            if (!GetAsUnsignedInt(optarg, initiator_id) || initiator_id > 7) {
                throw parser_exception("Invalid PiSCSI board ID " + to_string(initiator_id) + " (0-7)");
            }
            break;

        case 'f':
            filename = optarg;
            break;

        case 'I':
        	inquiry = true;
        	break;

        case 'b':
            if (!GetAsUnsignedInt(optarg, buffer_size) || buffer_size < MINIMUM_BUFFER_SIZE) {
                throw parser_exception("Buffer size must be at least " + to_string(MINIMUM_BUFFER_SIZE / 1024) + " KiB");
            }

            break;

        case 's':
        	scan_bus = true;
        	break;

        case 't':
            if (const string error = ProcessId(optarg, target_id, target_lun); !error.empty()) {
                throw parser_exception(error);
            }
            break;

        case 'v':
            if (!to_stdout) {
            	set_level(level::debug);
            }
            break;

        case 'V':
            if (!to_stdout) {
            	set_level(level::trace);
            }
            break;

        case 'a':
        	all_luns = true;
        	break;

        case 'r':
            restore = true;
            break;

        case 'p':
            properties_file = true;
            break;

        default:
            break;
        }
    }

    if (!scan_bus && target_id == -1) {
    	throw parser_exception("Missing target ID");
    }

    if (target_id == initiator_id) {
        throw parser_exception("Target ID and PiSCSI board ID must not be identical");
    }

    if ((filename.empty() && !scan_bus && !inquiry && !to_stdout) || properties_file) {
        throw parser_exception("Missing filename");
    }

    if (target_lun == -1) {
    	target_lun = 0;
    }

    if (scan_bus) {
    	inquiry = false;
    }

    buffer = vector<uint8_t>(buffer_size);
}

bool ScsiDump::Execute(scsi_command cmd, span<uint8_t> cdb, int length)
{
    spdlog::trace("Executing " + command_mapping.find(cmd)->second.second);

    phase_executor->Reset(target_id, target_lun);

    if (!phase_executor->Arbitration()) {
		bus->Reset();
		return false;
    }

    if (!phase_executor->Selection()) {
		Reset();
		return false;
	}

    // Timeout 3 s
	auto now = chrono::steady_clock::now();
    while ((chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - now).count()) < 3) {
        bus->Acquire();

        if (bus->GetREQ()) {
        	try {
        		if (Dispatch(bus->GetPhase(), cmd, cdb, length)) {
        			now = chrono::steady_clock::now();
        		}
        		else {
        			bus->Reset();
         			return !phase_executor->GetStatus();
        		}
        	}
        	catch (const phase_exception& e) {
        		cerr << "Error: " << e.what() << endl;
        		bus->Reset();
        		return false;
        	}
        }
    }

    return false;
}

bool ScsiDump::Dispatch(phase_t phase, scsi_command cmd, span<uint8_t> cdb, int length)
{
	spdlog::trace(string("Handling ") + BUS::GetPhaseStrRaw(phase) + " phase");

	switch (phase) {
		case phase_t::command:
			phase_executor->Command(cmd, cdb);
			break;

		case phase_t::status:
			phase_executor->Status();
			break;

		case phase_t::datain:
			phase_executor->DataIn(buffer, length);
			break;

    	case phase_t::dataout:
    		phase_executor->DataOut(buffer, length);
    		break;

    	case phase_t::msgin:
    		phase_executor->MsgIn();
    		return false;

    	case phase_t::msgout:
    		phase_executor->MsgOut();
    		break;

    	default:
    		throw phase_exception(string("Ignoring ") + BUS::GetPhaseStrRaw(phase) + " phase");
    		break;
	}

    return true;
}

void ScsiDump::Reset() const
{
	bus->SetDAT(0);
	bus->SetBSY(false);
	bus->SetSEL(false);
	bus->SetATN(false);
}

bool ScsiDump::TestUnitReady()
{
	vector<uint8_t> cdb(6);

    return Execute(scsi_command::eCmdTestUnitReady, cdb, 0);
}

bool ScsiDump::Inquiry()
{
	vector<uint8_t> cdb(6);
	cdb[4] = 0xff;

    return Execute(scsi_command::eCmdInquiry, cdb, 256);
}

pair<uint64_t, uint32_t> ScsiDump::ReadCapacity()
{
	vector<uint8_t> cdb(10);

    if (!Execute(scsi_command::eCmdReadCapacity10, cdb, 8)) {
    	return { 0, 0 };
    }

    uint64_t capacity = (static_cast<uint32_t>(buffer[0]) << 24) | (static_cast<uint32_t>(buffer[1]) << 16) |
    		(static_cast<uint32_t>(buffer[2]) << 8) | static_cast<uint32_t>(buffer[3]);

    int sector_size_offset = 4;

    if (static_cast<int32_t>(capacity) == -1) {
    	cdb.resize(16);
       	// READ CAPACITY(16), not READ LONG(16)
    	cdb[1] = 0x10;

    	if (!Execute(scsi_command::eCmdReadCapacity16_ReadLong16, cdb, 14)) {
        	return { 0, 0 };
    	}

    	capacity = (static_cast<uint64_t>(buffer[0]) << 56) | (static_cast<uint64_t>(buffer[1]) << 48) |
    			(static_cast<uint64_t>(buffer[2]) << 40) | (static_cast<uint64_t>(buffer[3]) << 32) |
				(static_cast<uint64_t>(buffer[4]) << 24) | (static_cast<uint64_t>(buffer[5]) << 16) |
				(static_cast<uint64_t>(buffer[6]) << 8) | static_cast<uint64_t>(buffer[7]);

    	sector_size_offset = 8;
    }

    const uint32_t sector_size = (static_cast<uint32_t>(buffer[sector_size_offset]) << 24) |
    		(static_cast<uint32_t>(buffer[sector_size_offset + 1]) << 16) |
			(static_cast<uint32_t>(buffer[sector_size_offset + 2]) << 8) |
			static_cast<uint32_t>(buffer[sector_size_offset + 3]);

    return { capacity + 1, sector_size };
}

bool ScsiDump::ReadWrite(uint32_t bstart, uint32_t blength, int length, bool isWrite)
{
	vector<uint8_t> cdb(10);
	cdb[2] = static_cast<uint8_t>(bstart >> 24);
    cdb[3] = static_cast<uint8_t>(bstart >> 16);
    cdb[4] = static_cast<uint8_t>(bstart >> 8);
    cdb[5] = static_cast<uint8_t>(bstart);
    cdb[7] = static_cast<uint8_t>(blength >> 8);
    cdb[8] = static_cast<uint8_t>(blength);

    return Execute(isWrite ? scsi_command::eCmdWrite10 : scsi_command::eCmdRead10, cdb, length);
}

void ScsiDump::SynchronizeCache()
{
	vector<uint8_t> cdb(10);

	Execute(scsi_command::eCmdSynchronizeCache10, cdb, 0);
}

set<int> ScsiDump::ReportLuns()
{
	const int TRANSFER_LENGTH = 512;

	vector<uint8_t> cdb(12);
	cdb[8] = static_cast<uint8_t>(TRANSFER_LENGTH >> 8);
	cdb[9] = static_cast<uint8_t>(TRANSFER_LENGTH);

	// Assume 8 LUNs in case REPORT LUNS is not available
	if (!Execute(scsi_command::eCmdReportLuns, cdb, TRANSFER_LENGTH)) {
		spdlog::trace("Device does not support REPORT LUNS");
		return { 0, 1, 2, 3, 4, 5, 6, 7 };
	}

	const auto lun_count = (static_cast<size_t>(buffer[2]) << 8) | static_cast<size_t>(buffer[3]) / 8;
	spdlog::trace("Device reported LUN count of " + to_string(lun_count));

	set<int> luns;
	int offset = 8;
	for (size_t i = 0; i < lun_count && offset < TRANSFER_LENGTH - 8; i++, offset += 8) {
		const uint64_t lun =
				(static_cast<uint64_t>(buffer[offset]) << 56) | (static_cast<uint64_t>(buffer[offset + 1]) << 48) |
    			(static_cast<uint64_t>(buffer[offset + 1]) << 40) | (static_cast<uint64_t>(buffer[offset + 3]) << 32) |
				(static_cast<uint64_t>(buffer[offset + 4]) << 24) | (static_cast<uint64_t>(buffer[offset + 5]) << 16) |
				(static_cast<uint64_t>(buffer[offset + 6]) << 8) | static_cast<uint64_t>(buffer[offset + 7]);
		if (lun < static_cast<uint64_t>(ControllerManager::GetScsiLunMax())) {
			luns.insert(static_cast<int>(lun));
		}
		else {
			spdlog::trace("Device reported invalid LUN " + to_string(lun));
		}
	}

	return luns;
}


int ScsiDump::run(span<char *> args)
{
	to_stdout = !isatty(STDOUT_FILENO);

	// Prevent any logging when dumping to stdout
	if (to_stdout) {
		spdlog::set_level(level::off);
	}

	// When dumping to stdout use stderr instead of stdout for console output
	ostream& console = to_stdout ? cerr : cout;

	if (!Banner(console, args)) {
        return EXIT_SUCCESS;
    }

    try {
        ParseArguments(args);
    }
    catch (const parser_exception& e) {
        cerr << "Error: " << e.what() << endl;
        return EXIT_FAILURE;
    }

    if (getuid()) {
    	cerr << "Error: GPIO bus access requires root permissions. Are you running as root?" << endl;
        return EXIT_FAILURE;
    }

#if defined(__x86_64__) || defined(__X86__) || !defined(__linux__)
    cerr << "Error: No PiSCSI hardware support" << endl;
    return EXIT_FAILURE;
#endif

    if (!Init()) {
		cerr << "Error: Can't initialize bus" << endl;
        return EXIT_FAILURE;
    }

    phase_executor = make_unique<PhaseExecutor>(*bus, initiator_id);

    try {
    	if (scan_bus) {
    		ScanBus(console);
    	}
    	else if (inquiry) {
    		DisplayBoardId(console);

    		inquiry_info_t inq_info;
    		if (DisplayInquiry(console, inq_info, false) && properties_file && !filename.empty()) {
    			inq_info.GeneratePropertiesFile(console, filename + ".properties");
    		}
    	}
    	else {
    		if (const string error = DumpRestore(console); !error.empty()) {
    			cerr << "Error: " << error << endl;
    		}
    	}
    }
    catch (const phase_exception& e) {
    	cerr << "Error: " << e.what() << endl;

    	CleanUp();

    	return EXIT_FAILURE;
    }

    CleanUp();

    return EXIT_SUCCESS;
}

void ScsiDump::DisplayBoardId(ostream& console) const
{
    console << DIVIDER << "\nPiSCSI board ID is " << initiator_id << "\n";
}

void ScsiDump::ScanBus(ostream& console)
{
    DisplayBoardId(console);

	for (target_id = 0; target_id < ControllerManager::GetScsiIdMax(); target_id++) {
		if (initiator_id == target_id) {
			continue;
		}

		target_lun = 0;
		inquiry_info_t inq_info;
		if (!DisplayInquiry(console, inq_info, false) || !all_luns) {
			// Continue with next ID if there is no LUN 0 or only LUN 0 should be scanned
			continue;
		}

		auto luns = ReportLuns();
		// LUN 0 has already been dealt with
		luns.erase(0);

		for (const auto lun : luns) {
			target_lun = lun;
			DisplayInquiry(console, inq_info, false);
		}
	}
}

bool ScsiDump::DisplayInquiry(ostream& console, inquiry_info_t& inq_info, bool check_type)
{
    console << DIVIDER << "\nTarget device is " << target_id << ":" << target_lun << "\n" << flush;

    inq_info = {};

    if (!Inquiry()) {
    	return false;
    }

    const auto type = static_cast<byte>(buffer[0]);
    if ((type & byte{0x1f}) == byte{0x1f}) {
    	// Requested LUN is not available
    	return false;
    }

    array<char, 17> str = {};
    memcpy(str.data(), &buffer[8], 8);
    inq_info.vendor = string(str.data());
    console << "Vendor:      " << inq_info.vendor << "\n";

    str.fill(0);
    memcpy(str.data(), &buffer[16], 16);
    inq_info.product = string(str.data());
    console << "Product:     " << inq_info.product << "\n";

    str.fill(0);
    memcpy(str.data(), &buffer[32], 4);
    inq_info.revision = string(str.data());
    console << "Revision:    " << inq_info.revision << "\n" << flush;

    if (const auto& t = DEVICE_TYPES.find(type & byte{0x1f}); t != DEVICE_TYPES.end()) {
    	console << "Device Type: " << (*t).second << "\n";
    }
    else {
    	console << "Device Type: Unknown\n";
    }

    console << "Removable:   " << (((static_cast<byte>(buffer[1]) & byte{0x80}) == byte{0x80}) ? "Yes" : "No") << "\n";

    if (check_type && type != static_cast<byte>(device_type::direct_access) &&
    		type != static_cast<byte>(device_type::cd_rom) && type != static_cast<byte>(device_type::optical_memory)) {
    	cerr << "Error: Invalid device type, supported types for dump/restore are DIRECT ACCESS,"
    			<< " CD-ROM/DVD/BD and OPTICAL MEMORY" << endl;
    	return false;
    }

    return true;
}

string ScsiDump::DumpRestore(ostream& console)
{
	inquiry_info_t inq_info;
	if (!GetDeviceInfo(console, inq_info)) {
		return "Can't get device information";
	}

	fstream fs;
	if (!to_stdout) {
		fs.open(filename, (restore ? ios::in : ios::out) | ios::binary);
		if (fs.fail()) {
			return "Can't open image file '" + filename + "': " + strerror(errno);
		}
	}

	ostream& out = to_stdout ? cout : fs;

	const off_t disk_size = inq_info.capacity * inq_info.sector_size;

    size_t effective_size;
    if (restore) {
        off_t size;
        try {
        	size = file_size(path(filename));
        }
        catch (const filesystem_error& e) {
        	return string("Can't determine image file size: ") + e.what();
        }

        effective_size = min(size, disk_size);

        console << "Restore image file size: " << size << " bytes\n" << flush;
        if (size > disk_size) {
            console << "Warning: Image file size of " << size
            		<< " byte(s) is larger than disk size of " << disk_size << " bytes(s)\n" << flush;
        } else if (size < disk_size) {
        	console << "Warning: Image file size of " << size
            		<< " byte(s) is smaller than disk size of " << disk_size << " bytes(s)\n" << flush;
        }
    } else {
    	effective_size = disk_size;
    }

    if (!effective_size) {
    	console << "Nothing to do, effective size is 0\n" << flush;
    	return "";
    }

    console << "Starting " << (restore ? "restore" : "dump") << ", buffer size is " << buffer.size()
    		<< " bytes\n\n" << flush;

    int sector_offset = 0;
    auto remaining = effective_size;

    const auto start_time = chrono::high_resolution_clock::now();

    while (remaining) {
    	const auto byte_count = static_cast<int>(min(remaining, buffer.size()));
        auto sector_count = byte_count / inq_info.sector_size;
        if (byte_count % inq_info.sector_size) {
        	++sector_count;
        }

        spdlog::debug("Remaining bytes: " + to_string(remaining));
        spdlog::debug("Next sector: " + to_string(sector_offset));
        spdlog::debug("Sector count: " + to_string(sector_count));
        spdlog::debug("SCSI transfer size: " + to_string(sector_count * inq_info.sector_size));
        spdlog::debug("File chunk size: " + to_string(byte_count));

        if (restore) {
        	fs.read((char*)buffer.data(), byte_count);
        	if (fs.fail()) {
        		return "Error reading from file '" + filename + "'";
        	}
        	if (!ReadWrite(sector_offset, sector_count, sector_count * inq_info.sector_size, true)) {
        		return "Error writing to device";
        	}
        } else {
            if (!ReadWrite(sector_offset, sector_count, sector_count * inq_info.sector_size, false)) {
            	return "Error reading from device";
            }
            out.write((const char*)buffer.data(), byte_count);
            if (out.fail()) {
            	return "Error writing to file '" + filename + "'";
            }
        }

        sector_offset += sector_count;
        remaining -= byte_count;

        console << setw(3) << (effective_size - remaining) * 100 / effective_size << "% ("
        		<< effective_size - remaining << "/" << effective_size << ")\n" << flush;
    }

    if (restore) {
    	// Ensure that if the target device is also a PiSCSI instance its image file becomes complete immediately
    	SynchronizeCache();
    }

    const auto stop_time = chrono::high_resolution_clock::now();
    const auto duration = chrono::duration_cast<chrono::seconds>(stop_time - start_time).count();

    console << DIVIDER << "\n";
    console << "Transferred " << effective_size / 1024 / 1024 << " MiB (" << effective_size << " bytes)\n";
    console << "Total time: " << duration << " seconds (" << duration / 60 << " minutes)\n";
    console << "Average transfer rate: " << effective_size / duration << " bytes per second ("
    		<< effective_size / 1024 / duration << " KiB per second)\n";
    console << DIVIDER << "\n" << flush;

    if (properties_file && !restore) {
        inq_info.GeneratePropertiesFile(console, filename + ".properties");
    }

    return "";
}

bool ScsiDump::GetDeviceInfo(ostream& console, inquiry_info_t& inq_info)
{
    DisplayBoardId(console);

    if (!DisplayInquiry(console, inq_info, true)) {
    	return false;
    }

    if (!TestUnitReady()) {
    	spdlog::trace("Device is not ready");
    	return false;
    }

    const auto [capacity, sector_size] = ReadCapacity();
    if (!capacity || !sector_size) {
    	spdlog::trace("Can't get device capacity");
    	return false;
    }

    inq_info.capacity = capacity;
    inq_info.sector_size = sector_size;

    console << "Sectors:     " << capacity << "\n"
    		<< "Sector size: " << sector_size << " bytes\n"
			<< "Capacity:    " << sector_size * capacity / 1024 / 1024 << " MiB (" << sector_size * capacity
			<< " bytes)\n"
			<< DIVIDER << "\n\n"
			<< flush;

    return true;
}

void ScsiDump::inquiry_info::GeneratePropertiesFile(ostream& console, const string& property_file) const
{
	ofstream prop(property_file);

	prop << "{\n"
    		<< "    \"vendor\": \"" << vendor << "\",\n"
			<< "    \"product\": \"" << product << "\",\n"
			<< "    \"revision\": \"" << revision << "\"";
    if (sector_size) {
    	prop << ",\n    \"block_size\": \"" << sector_size << "\"";
    }
    prop << "\n}\n";

    if (prop.fail()) {
        cerr << "Error: Can't create properties file '" + property_file + "': " << strerror(errno) << endl;
    }
    else {
    	console << "Created properties file '" + property_file + "'\n" << flush;
    }
}
