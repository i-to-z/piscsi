//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022-2023 Uwe Seimet
//
// Host Services with support for realtime clock, shutdown and command execution
//
//---------------------------------------------------------------------------

//
// Features of the host services device:
//
// 1. Vendor-specific mode page 0x20 returns the current date and time, see mode_page_datetime
//
// 2. START/STOP UNIT shuts down PiSCSI or shuts down/reboots the Raspberry Pi
//   a) !start && !load (STOP): Shut down PiSCSI
//   b) !start && load (EJECT): Shut down the Raspberry Pi
//   c) start && load (LOAD): Reboot the Raspberry Pi
//
// 3. Remote command execution via SCSI, using this custom SCSI command:
//
// +==============================================================================
// |  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
// |Byte |        |        |        |        |        |        |        |        |
// |=====+========================================================================
// | 0   |                           Operation code (c0h)                        |
// |-----+-----------------------------------------------------------------------|
// | 1   | Logical unit number      |        |  B_OUT |  J_OUT |  B_IN  |  J_IN  |
// |-----+-----------------------------------------------------------------------|
// | 2   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 3   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 4   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 5   | (MSB)                                                                 |
// |-----+---                        Input data length                           |
// | 6   |                                                                 (LSB) |
// |-----+-----------------------------------------------------------------------|
// | 7   | (MSB)                                                                 |
// |-----+---                        Byte transfer length                        |
// | 8   |                                                                 (LSB) |
// |-----+-----------------------------------------------------------------------|
// | 9   |                           Control                                     |
// +==============================================================================
//
// J_IN, B_IN, J_OUT and B_OUT control the input and output formats.
// There can only be one input and one output format. These formats do not have to be identical.
// Note that this command requires both a DATA OUT (input data length) and a DATA IN (byte transfer length) phase,
// which is unusual.
//

#include "shared/piscsi_exceptions.h"
#include "shared/protobuf_util.h"
#include "controllers/scsi_controller.h"
#include "scsi_command_util.h"
#include "host_services.h"
#include <google/protobuf/util/json_util.h>
#include <algorithm>
#include <chrono>

using namespace std::chrono;
using namespace google::protobuf::util;
using namespace scsi_defs;
using namespace scsi_command_util;
using namespace protobuf_util;

bool HostServices::Init(const param_map& params)
{
    ModePageDevice::Init(params);

    AddCommand(scsi_command::eCmdTestUnitReady, [this] { TestUnitReady(); });
    AddCommand(scsi_command::eCmdStartStop, [this] { StartStopUnit(); });
    AddCommand(scsi_command::eCmdExecute, [this] { Execute(); });

	SetReady(true);

	return true;
}

void HostServices::TestUnitReady()
{
	// Always successful
	EnterStatusPhase();
}

vector<uint8_t> HostServices::InquiryInternal() const
{
	return HandleInquiry(device_type::processor, scsi_level::spc_3, false);
}

void HostServices::StartStopUnit() const
{
	const bool start = GetController()->GetCmdByte(4) & 0x01;
	const bool load = GetController()->GetCmdByte(4) & 0x02;

	if (!start) {
		if (load) {
			GetController()->ScheduleShutdown(AbstractController::piscsi_shutdown_mode::STOP_PI);
		}
		else {
			GetController()->ScheduleShutdown(AbstractController::piscsi_shutdown_mode::STOP_PISCSI);
		}
	}
	else if (load) {
		GetController()->ScheduleShutdown(AbstractController::piscsi_shutdown_mode::RESTART_PI);
	}
	else {
		throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_cdb);
	}

	EnterStatusPhase();
}

void HostServices::Execute()
{
    const int formats = GetController()->GetCmdByte(1);
    json_in = formats & 0x01;
    json_out = formats & 0x04;
    const bool bin_in = formats & 0x02;

    if (const bool bin_out = formats & 0x08;
        !formats || !(json_in || bin_in) || !(json_out || bin_out) || (json_in && bin_in) || (json_out && bin_out)) {
        throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_cdb);
    }

    const auto length = static_cast<size_t>(GetInt16(GetController()->GetCmd(), 5));
    if (!length) {
        throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_cdb);
    }

    // The custom SCSI Execute command supports transfers of up to 65535 bytes
    GetController()->AllocateBuffer(65536);

    GetController()->SetLength(static_cast<uint32_t>(length));
    GetController()->SetByteTransfer(true);

    EnterDataOutPhase();
}

int HostServices::ModeSense6(cdb_t cdb, vector<uint8_t>& buf) const
{
	// Block descriptors cannot be returned
	if (!(cdb[1] & 0x08)) {
		throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_cdb);
	}

	const auto length = static_cast<int>(min(buf.size(), static_cast<size_t>(cdb[4])));
	fill_n(buf.begin(), length, 0);

	// 4 bytes basic information
	const int size = AddModePages(cdb, buf, 4, length, 255);

	buf[0] = (uint8_t)size;

	return size;
}

int HostServices::ModeSense10(cdb_t cdb, vector<uint8_t>& buf) const
{
	// Block descriptors cannot be returned
	if (!(cdb[1] & 0x08)) {
		throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_cdb);
	}

	const auto length = static_cast<int>(min(buf.size(), static_cast<size_t>(GetInt16(cdb, 7))));
	fill_n(buf.begin(), length, 0);

	// 8 bytes basic information
	const int size = AddModePages(cdb, buf, 8, length, 65535);

	SetInt16(buf, 0, size);

	return size;
}

void HostServices::SetUpModePages(map<int, vector<byte>>& pages, int page, bool changeable) const
{
	if (page == 0x20 || page == 0x3f) {
		AddRealtimeClockPage(pages, changeable);
	}
}

void HostServices::AddRealtimeClockPage(map<int, vector<byte>>& pages, bool changeable) const
{
	pages[32] = vector<byte>(10);

	if (!changeable) {
		const auto now = system_clock::now();
		const time_t t = system_clock::to_time_t(now);
		tm localtime;
		localtime_r(&t, &localtime);

		mode_page_datetime datetime;
		datetime.major_version = 0x01;
		datetime.minor_version = 0x00;
		datetime.year = (uint8_t)localtime.tm_year;
		datetime.month = (uint8_t)localtime.tm_mon;
		datetime.day = (uint8_t)localtime.tm_mday;
		datetime.hour = (uint8_t)localtime.tm_hour;
		datetime.minute = (uint8_t)localtime.tm_min;
		// Ignore leap second for simplicity
		datetime.second = (uint8_t)(localtime.tm_sec < 60 ? localtime.tm_sec : 59);

		memcpy(&pages[32][2], &datetime, sizeof(datetime));
	}
}

bool HostServices::WriteByteSequence(span<const uint8_t> buf)
{
    const auto length = static_cast<size_t>(GetInt16(GetController()->GetCmd(), 5));

    PbCommand command;
    bool status;

    if (json_in) {
        string cmd((const char *)buf.data(), length);
        status = JsonStringToMessage(cmd, &command).ok();
    }
    else {
        status = command.ParseFromArray(buf.data(), static_cast<int>(length));
    }

    if (!status) {
        LogTrace("Error deserializing protobuf input data");
        return false;
    }

    CommandContext context(command, piscsi_image.GetDefaultFolder(), protobuf_util::GetParam(command, "locale"));
    PbResult result;
    dispatcher->DispatchCommand(context, result);

    const auto allocation_length = static_cast<size_t>(GetInt16(GetController()->GetCmd(), 7));

    int size = 0;
    if (json_out) {
        string json;
        status = MessageToJsonString(result, &json).ok();
        if (status) {
            size = static_cast<int>(min(allocation_length, json.size()));
            if (size > 65535) {
                return false;
             }

            memcpy(GetController()->GetBuffer().data(), json.data(), size);
        }
    }
    else {
        const string data = result.SerializeAsString();
        size = static_cast<int>(min(allocation_length, data.size()));
        if (size > 65535) {
            return false;
         }

        memcpy(GetController()->GetBuffer().data(), data.data(), size);
        status = true;
    }

    if (!status) {
        LogTrace("Error serializing protobuf output data");
        return false;
    }

    if (!size) {
        EnterStatusPhase();
    }
    else {
        GetController()->SetLength(static_cast<uint32_t>(size));

        EnterDataInPhase();
    }

    return true;
}
