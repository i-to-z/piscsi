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
// 3. Remote command execution via SCSI, using these vendor-specific SCSI commands:
//
//   a) ExecuteOperation
//
// +==============================================================================
// |  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
// |Byte |        |        |        |        |        |        |        |        |
// |=====+========================================================================
// | 0   |                           Operation code (c0h)                        |
// |-----+-----------------------------------------------------------------------|
// | 1   | Logical unit number      |     Reserved    |  TEXT  |  JSON  |  BIN   |
// |-----+-----------------------------------------------------------------------|
// | 2   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 3   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 4   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 5   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 6   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 7   | (MSB)                                                                 |
// |-----+---                        Byte transfer length                        |
// | 8   |                                                                 (LSB) |
// |-----+-----------------------------------------------------------------------|
// | 9   |                           Control                                     |
// +==============================================================================
//
//   b) ReadOperationResult
//
// +==============================================================================
// |  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
// |Byte |        |        |        |        |        |        |        |        |
// |=====+========================================================================
// | 0   |                           Operation code (c1h)                        |
// |-----+-----------------------------------------------------------------------|
// | 1   | Logical unit number      |     Reserved    |  TEXT  |  JSON  |  BIN   |
// |-----+-----------------------------------------------------------------------|
// | 2   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 3   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 4   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 5   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 6   |                           Reserved                                    |
// |-----+-----------------------------------------------------------------------|
// | 7   | (MSB)                                                                 |
// |-----+---                        Byte transfer length                        |
// | 8   |                                                                 (LSB) |
// |-----+-----------------------------------------------------------------------|
// | 9   |                           Control                                     |
// +==============================================================================
//
// The piscsi commands that can be executed are defined in the piscsi_interface.proto file.
// The BIN, JSON and TEXT flags control the input and output format of the protobuf data.
// Exactly one of them must be set. Input and output format do not have to be identical.
//

#include "shared/piscsi_exceptions.h"
#include "shared/protobuf_util.h"
#include "controllers/scsi_controller.h"
#include "scsi_command_util.h"
#include "host_services.h"
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/text_format.h>
#include <algorithm>
#include <chrono>

using namespace std::chrono;
using namespace google::protobuf;
using namespace google::protobuf::util;
using namespace piscsi_interface;
using namespace scsi_defs;
using namespace scsi_command_util;
using namespace protobuf_util;

bool HostServices::Init(const param_map& params)
{
    ModePageDevice::Init(params);

    AddCommand(scsi_command::eCmdTestUnitReady, [this] { TestUnitReady(); });
    AddCommand(scsi_command::eCmdStartStop, [this] { StartStopUnit(); });
    AddCommand(scsi_command::eCmdExecuteOperation, [this] { ExecuteOperation(); });
    AddCommand(scsi_command::eCmdReadOperationResult, [this] { ReadOperationResult(); });

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

void HostServices::ExecuteOperation()
{
    input_format = ConvertFormat(GetController()->GetCmdByte(1) & 0b00000111);

    const auto length = static_cast<size_t>(GetInt16(GetController()->GetCmd(), 7));
    if (!length) {
        throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_cdb);
    }

    GetController()->SetLength(static_cast<uint32_t>(length));
    GetController()->SetByteTransfer(true);

    EnterDataOutPhase();
}

void HostServices::ReadOperationResult()
{
    const protobuf_format output_format = ConvertFormat(GetController()->GetCmdByte(1) & 0b00000111);

    const auto& it = operation_results.find(GetController()->GetInitiatorId());
    if (it == operation_results.end()) {
        throw scsi_exception(sense_key::aborted_command);
    }
    const auto& operation_result = it->second;

    const auto allocation_length = static_cast<size_t>(GetInt16(GetController()->GetCmd(), 7));

    string data;
    switch (output_format) {
    case protobuf_format::binary:
        data = operation_result->SerializeAsString();
        break;

    case protobuf_format::json:
        MessageToJsonString(*operation_result, &data);
        break;

    case protobuf_format::text:
        TextFormat::PrintToString(*operation_result, &data);
        break;

    default:
        assert(false);
        break;
    }

    operation_results.erase(GetController()->GetInitiatorId());

    auto length = static_cast<int>(min(allocation_length, data.size()));
    if (length > 65535) {
        throw scsi_exception(sense_key::aborted_command);
    }

    if (!length) {
        EnterStatusPhase();
    }
    else {
        memcpy(GetController()->GetBuffer().data(), data.data(), length);

        GetController()->SetLength(static_cast<uint32_t>(length));

        EnterDataInPhase();
    }
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
    const auto length = GetInt16(GetController()->GetCmd(), 7);

    PbCommand command;
    bool status;

    switch (input_format) {
    case protobuf_format::binary:
        status = command.ParseFromArray(buf.data(), length);
        break;

    case protobuf_format::json: {
        string cmd((const char*) buf.data(), length);
        status = JsonStringToMessage(cmd, &command).ok();
        break;
    }

    case protobuf_format::text: {
        string cmd((const char*) buf.data(), length);
        status = TextFormat::ParseFromString(cmd, &command);
        break;
    }

    default:
        assert(false);
        break;
    }

    if (!status) {
        LogTrace("Error deserializing protobuf input data");
        return false;
    }

    auto operation_result = make_shared<PbResult>();
    if (CommandContext context(command, piscsi_image.GetDefaultFolder(), protobuf_util::GetParam(command, "locale"));
        !dispatcher->DispatchCommand(context, *operation_result, fmt::format("(ID:LUN {0}:{1}) - ", GetId(), GetLun()))) {
        LogTrace("Failed to execute " + PbOperation_Name(command.operation()) + " operation");
        return false;
    }

    operation_results[GetController()->GetInitiatorId()] = operation_result;

    return true;
}

HostServices::protobuf_format HostServices::ConvertFormat(int format)
{
    switch (format) {
    case 0x01:
        return protobuf_format::binary;
        break;

    case 0x02:
        return protobuf_format::json;
        break;

    case 0x04:
        return protobuf_format::text;
        break;

    default:
        throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_cdb);
    }
}
