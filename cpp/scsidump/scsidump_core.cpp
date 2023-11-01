//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022 akuker
// Copyright (C) 2022-2023 Uwe Seimet
//
//---------------------------------------------------------------------------

// TODO Evaluate CHECK CONDITION after sending a command
// TODO Send IDENTIFY message in order to support LUNS > 7

#include "scsidump/scsidump_core.h"
#include "hal/gpiobus_factory.h"
#include "hal/systimer.h"
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
#include <iostream>
#include <sstream>
#include <iomanip>

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

bool ScsiDump::Banner(span<char *> args) const
{
    cout << piscsi_util::Banner("(Hard Disk Dump/Restore Utility)");

    if (args.size() < 2 || string(args[1]) == "-h" || string(args[1]) == "--help") {
        cout << "Usage: " << args[0] << " -t ID[:LUN] [-i BID] -f FILE [-v] [-r] [-s BUFFER_SIZE] [-p] [-I] [-S]\n"
             << " ID is the target device ID (0-" << (ControllerManager::GetScsiIdMax() - 1) << ").\n"
             << " LUN is the optional target device LUN (0-" << (ControllerManager::GetScsiLunMax() -1 ) << ")."
			 << " Default is 0.\n"
             << " BID is the PiSCSI board ID (0-7). Default is 7.\n"
             << " FILE is the dump file path.\n"
             << " BUFFER_SIZE is the transfer buffer size in bytes, at least " << MINIMUM_BUFFER_SIZE
             << " bytes. Default is 1 MiB.\n"
             << " -v Enable verbose logging.\n"
             << " -r Restore instead of dump.\n"
             << " -p Generate .properties file to be used with the PiSCSI web interface. Only valid for dump mode.\n"
			 << " -I Display INQUIRY data of ID[:LUN].\n"
			 << " -S Scan SCSI bus for devices.\n"
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
    while ((opt = getopt(static_cast<int>(args.size()), args.data(), "i:f:s:t:rvpIS")) != -1) {
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

        case 's':
            if (!GetAsUnsignedInt(optarg, buffer_size) || buffer_size < MINIMUM_BUFFER_SIZE) {
                throw parser_exception("Buffer size must be at least " + to_string(MINIMUM_BUFFER_SIZE / 1024) + " KiB");
            }

            break;

        case 'S':
        	scan_bus = true;
        	break;

        case 't':
            if (const string error = ProcessId(optarg, target_id, target_lun); !error.empty()) {
                throw parser_exception(error);
            }
            break;

        case 'v':
            set_level(level::debug);
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

    if (!scan_bus && !inquiry && filename.empty()) {
        throw parser_exception("Missing filename");
    }

    if (!scan_bus && target_id == -1) {
    	throw parser_exception("Missing target ID");
    }

    if (target_id == initiator_id) {
        throw parser_exception("Target ID and PiSCSI board ID must not be identical");
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
    spdlog::debug("Executing " + command_mapping.find(cmd)->second.second);

    if (!Selection()) {
		spdlog::debug("SELECTION failed");

		bus->Reset();

		return false;
	}

	// Timeout (3000 ms)
	uint32_t now = SysTimer::GetTimerLow();
    while ((SysTimer::GetTimerLow() - now) < 3'000'000) {
        bus->Acquire();

        if (bus->GetREQ()) {
        	try {
        		if (Dispatch(bus->GetPhase(), cmd, cdb, length)) {
        			now = SysTimer::GetTimerLow();
        		}
        		else {
        			bus->Reset();
        			return true;
        		}
        	}
        	catch (const phase_exception& e) {
        		cerr << "Error: " << e.what() << endl;
        		return false;
        	}
        }
    }

    return false;
}

bool ScsiDump::Dispatch(phase_t phase, scsi_command cmd, span<uint8_t> cdb, int length)
{
	spdlog::debug(string("Handling ") + BUS::GetPhaseStrRaw(phase) + " phase");

	switch (phase) {
		case phase_t::command:
			Command(cmd, cdb);
			break;

		case phase_t::status:
			Status();
			break;

		case phase_t::datain:
			DataIn(length);
			break;

    	case phase_t::dataout:
    		DataOut(length);
    		break;

    	case phase_t::msgin:
    		MsgIn();
    		return false;

    	case phase_t::msgout:
    		MsgOut();
    		break;

    	default:
    		throw phase_exception(string("Ignoring ") + BUS::GetPhaseStrRaw(phase) + " phase");
    		break;
	}

    return true;
}

bool ScsiDump::Selection()
{
    // Set initiator and target ID
    auto data = static_cast<byte>(1 << initiator_id);
    data |= static_cast<byte>(1 << target_id);
    bus->SetDAT(static_cast<uint8_t>(data));

    bus->SetSEL(true);

    // Request MESSAGE OUT for IDENTIFY
    bus->SetATN(true);

    if (!WaitForBusy()) {
    	return false;
    }

    bus->SetSEL(false);

    return true;
}

void ScsiDump::Command(scsi_command cmd, span<uint8_t> cdb)
{
    cdb[0] = static_cast<uint8_t>(cmd);
    cdb[1] = static_cast<uint8_t>(static_cast<byte>(cdb[1]) | static_cast<byte>(target_lun << 5));
    if (static_cast<int>(cdb.size()) !=
        bus->SendHandShake(cdb.data(), static_cast<int>(cdb.size()), BUS::SEND_NO_DELAY)) {
        bus->Reset();

        throw phase_exception(command_mapping.find(cmd)->second.second + string(" failed"));
    }
}

void ScsiDump::Status()
{
    if (array<uint8_t, 256> buf; bus->ReceiveHandShake(buf.data(), 1) != 1) {
        throw phase_exception("STATUS failed");
    }
}

void ScsiDump::DataIn(int length)
{
    if (!bus->ReceiveHandShake(buffer.data(), length)) {
        throw phase_exception("DATA IN failed");
    }
}

void ScsiDump::DataOut(int length)
{
    if (!bus->SendHandShake(buffer.data(), length, BUS::SEND_NO_DELAY)) {
        throw phase_exception("DATA OUT failed");
    }
}

void ScsiDump::MsgIn()
{
    if (array<uint8_t, 256> buf; bus->ReceiveHandShake(buf.data(), 1) != 1) {
        throw phase_exception("MESSAGE IN failed");
    }
}

void ScsiDump::MsgOut()
{
	array<uint8_t, 1> buf;

	// IDENTIFY
	buf[0] = static_cast<uint8_t>(target_lun | 0x80);

	if (bus->SendHandShake(buf.data(), buf.size(), BUS::SEND_NO_DELAY) != buf.size()) {
        throw phase_exception("MESSAGE OUT failed");
    }

	bus->SetATN(false);
}

void ScsiDump::TestUnitReady()
{
	vector<uint8_t> cdb(6);

    Execute(scsi_command::eCmdTestUnitReady, cdb, 0);
}

void ScsiDump::RequestSense()
{
	vector<uint8_t> cdb(6);
    cdb[4] = 0xff;

    Execute(scsi_command::eCmdRequestSense, cdb, 256);
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

    Execute(scsi_command::eCmdReadCapacity10, cdb, 8);

    uint64_t capacity = (static_cast<uint32_t>(buffer[0]) << 24) | (static_cast<uint32_t>(buffer[1]) << 16) |
    		(static_cast<uint32_t>(buffer[2]) << 8) | static_cast<uint32_t>(buffer[3]);

    int sector_size_offset = 4;

    if (static_cast<int32_t>(capacity) == -1) {
    	cdb.resize(16);
       	// READ CAPACITY(16), not READ LONG(16)
    	cdb[1] = 0x10;

    	Execute(scsi_command::eCmdReadCapacity16_ReadLong16, cdb, 14);

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

    return { capacity, sector_size };
}

void ScsiDump::Read(uint32_t bstart, uint32_t blength, int length)
{
	vector<uint8_t> cdb(10);
	cdb[2] = (uint8_t)(bstart >> 24);
    cdb[3] = (uint8_t)(bstart >> 16);
    cdb[4] = (uint8_t)(bstart >> 8);
    cdb[5] = (uint8_t)bstart;
    cdb[7] = (uint8_t)(blength >> 8);
    cdb[8] = (uint8_t)blength;

    Execute(scsi_command::eCmdRead10, cdb, length);
}

void ScsiDump::Write(uint32_t bstart, uint32_t blength, int length)
{
	vector<uint8_t> cdb(10);
    cdb[2] = (uint8_t)(bstart >> 24);
    cdb[3] = (uint8_t)(bstart >> 16);
    cdb[4] = (uint8_t)(bstart >> 8);
    cdb[5] = (uint8_t)bstart;
    cdb[7] = (uint8_t)(blength >> 8);
    cdb[8] = (uint8_t)blength;

    Execute(scsi_command::eCmdWrite10, cdb, length);
}

bool ScsiDump::WaitForBusy() const
{
    // Wait for busy for up to 2 s
    int count = 10000;
    do {
        // Wait 20 ms
        const timespec ts = {.tv_sec = 0, .tv_nsec = 20 * 1000};
        nanosleep(&ts, nullptr);
        bus->Acquire();
        if (bus->GetBSY()) {
            break;
        }
    } while (count--);

    // Success if the target is busy
    return bus->GetBSY();
}

int ScsiDump::run(span<char *> args)
{
    if (!Banner(args)) {
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

#ifndef USE_SEL_EVENT_ENABLE
    cerr << "Error: No PiSCSI hardware support" << endl;
    return EXIT_FAILURE;
#endif

    if (!Init()) {
		cerr << "Error: Can't initialize bus" << endl;
        return EXIT_FAILURE;
    }

    try {
    	if (scan_bus) {
    		ScanBus();
    	}
    	else if (inquiry) {
    		DisplayBoardId();

    		inquiry_info_t inq_info;
    		DisplayInquiry(inq_info, false);
    	}
    	else {
    		if (const string error = DumpRestore(); !error.empty()) {
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

void ScsiDump::DisplayBoardId() const
{
    cout << DIVIDER << "\nPiSCSI board ID is " << initiator_id << "\n";
}

void ScsiDump::ScanBus()
{
    DisplayBoardId();

	for (target_id = 0; target_id < ControllerManager::GetScsiIdMax(); target_id++) {
		if (initiator_id == target_id) {
			continue;
		}

		for (target_lun = 0; target_lun < 8; target_lun++) {
			inquiry_info_t inq_info;

			// Continue with next ID if there is no LUN 0
			if (!DisplayInquiry(inq_info, false) && !target_lun) {
				break;
			}
		}
	}
}

bool ScsiDump::DisplayInquiry(inquiry_info_t& inq_info, bool check_type)
{
    // Assert RST for 1 ms
    bus->SetRST(true);
    const timespec ts = {.tv_sec = 0, .tv_nsec = 1000 * 1000};
    nanosleep(&ts, nullptr);
    bus->SetRST(false);

    cout << DIVIDER << "\nTarget device is " << target_id << ":" << target_lun << "\n" << flush;

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
    cout << "Vendor:      " << inq_info.vendor << "\n";

    str.fill(0);
    memcpy(str.data(), &buffer[16], 16);
    inq_info.product = string(str.data());
    cout << "Product:     " << inq_info.product << "\n";

    str.fill(0);
    memcpy(str.data(), &buffer[32], 4);
    inq_info.revision = string(str.data());
    cout << "Revision:    " << inq_info.revision << "\n" << flush;

    if (const auto& t = DEVICE_TYPES.find(type & byte{0x1f}); t != DEVICE_TYPES.end()) {
    	cout << "Device Type: " << (*t).second << "\n";
    }
    else {
    	cout << "Device Type: Unknown\n";
    }

    cout << "Removable:   " << (((static_cast<byte>(buffer[1]) & byte{0x80}) == byte{0x80}) ? "Yes" : "No") << "\n";

    if (check_type && type != static_cast<byte>(device_type::direct_access) &&
    		type != static_cast<byte>(device_type::cd_rom) && type != static_cast<byte>(device_type::optical_memory)) {
    	cerr << "Invalid device type, supported types for dump/restore are DIRECT ACCESS, CD-ROM/DVD/BD and OPTICAL MEMORY" << endl;
    	return false;
    }

    return true;
}

string ScsiDump::DumpRestore()
{
	inquiry_info_t inq_info;
	if (!GetDeviceInfo(inq_info)) {
		return "Can't get device information";
	}

    fstream fs;
    fs.open(filename, (restore ? ios::in : ios::out) | ios::binary);

    if (fs.fail()) {
        return "Can't open image file '" + filename + "'";
    }

    const off_t disk_size = inq_info.capacity * inq_info.sector_size;

    size_t effective_size;
    if (restore) {
        off_t size;
        try {
        	size = file_size(path(filename));
        }
        catch (const filesystem_error& e) {
        	return string("Can't determine file size: ") + e.what();
        }

        effective_size = min(size, disk_size);

        cout << "Restore file size: " << size << " bytes\n";
        if (size > disk_size) {
            cout << "WARNING: File size of " << size
            		<< " byte(s) is larger than disk size of " << disk_size << " bytes(s)\n" << flush;
        } else if (size < disk_size) {
            cout << "WARNING: File size of " << size
            		<< " byte(s) is smaller than disk size of " << disk_size << " bytes(s)\n" << flush;
        }
    } else {
    	effective_size = disk_size;
    }

    if (!effective_size) {
    	cout << "Nothing to do, effective size is 0\n" << flush;
    	return "";
    }

    cout << "Starting " << (restore ? "restore" : "dump") << ", buffer size is " << buffer.size()
    		<< " bytes\n\n" << flush;

    auto sector_count = min(effective_size / inq_info.sector_size, buffer.size() / inq_info.sector_size);
    if (!sector_count) {
    	sector_count = 1;
    }
    auto length = min(sector_count * inq_info.sector_size, effective_size);
    int sector_offset = 0;
    auto remaining = effective_size;

    spdlog::debug("Chunk size: " + to_string(length));

    const auto start_time = chrono::high_resolution_clock::now();

    while (remaining) {
        const int bytes = length > inq_info.sector_size ? length : inq_info.sector_size;

        spdlog::debug("Remaining bytes: " + to_string(remaining));
        spdlog::debug("Next sector: " + to_string(sector_offset));
        spdlog::debug("Sector count: " + to_string(sector_count));
        spdlog::debug("Byte count: " + to_string(bytes));

        if (restore) {
            fs.read((char*)buffer.data(), length);
            Write(sector_offset, sector_count, bytes);
        } else {
            Read(sector_offset, sector_count, bytes);
            fs.write((const char*)buffer.data(), length);
        }

        if (fs.fail()) {
            return (restore ? "Reading from '" : "Writing to '") + filename + " failed";
        }

        sector_offset += sector_count;
        remaining -= length;

        if (remaining < length) {
        	sector_count = remaining / inq_info.sector_size;
            if (!sector_count) {
            	sector_count = 1;
            }
        	length = remaining;
        }

        cout << setw(3) << (effective_size - remaining) * 100 / effective_size << "% ("
        		<< effective_size - remaining << "/" << effective_size << ")\n" << flush;
    }

    const auto stop_time = chrono::high_resolution_clock::now();
    const auto duration = chrono::duration_cast<chrono::seconds>(stop_time - start_time).count();

    cout << DIVIDER << "\n";
    cout << "Transferred " << effective_size / 1024 / 1024 << " MiB (" << effective_size << " bytes)\n";
    cout << "Total time: " << duration << " seconds (" << duration / 60 << " minutes)\n";
    cout << "Average transfer rate: " << effective_size / duration << " bytes per second ("
    		<< effective_size / 1024 / duration << " KiB per second)\n";
    cout << DIVIDER << "\n";

    if (properties_file && !restore) {
        inq_info.GeneratePropertiesFile(filename + ".properties");
    }

    return "";
}

bool ScsiDump::GetDeviceInfo(inquiry_info_t& inq_info)
{
    DisplayBoardId();

    if (!DisplayInquiry(inq_info, true)) {
    	return false;
    }

    TestUnitReady();

    RequestSense();

    const auto [capacity, sector_size] = ReadCapacity();

    inq_info.capacity = capacity;
    inq_info.sector_size = sector_size;

    cout << "Sectors:     " << capacity << "\n"
    		<< "Sector size: " << sector_size << " bytes\n"
			<< "Capacity:    " << sector_size * capacity / 1024 / 1024 << " MiB (" << sector_size * capacity
			<< " bytes)\n"
			<< DIVIDER << "\n\n"
			<< flush;

    return true;
}

void ScsiDump::inquiry_info::GeneratePropertiesFile(const string& property_file) const
{
	ofstream prop_stream(property_file);

    prop_stream << "{" << endl;
    prop_stream << "   \"vendor\": \"" << vendor << "\"," << endl;
    prop_stream << "   \"product\": \"" << product << "\"," << endl;
    prop_stream << "   \"revision\": \"" << revision << "\"," << endl;
    prop_stream << "   \"block_size\": \"" << sector_size << "\"" << endl;
    prop_stream << "}" << endl;

    if (prop_stream.fail()) {
        spdlog::warn("Unable to create properties file '" + property_file + "'");
    }
}
