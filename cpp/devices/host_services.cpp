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
    bin_in = formats & 0x02;
    json_out = formats & 0x04;
    bin_out = formats & 0x08;

    if (!formats || !(json_in || bin_in) || !(json_out || bin_out) || (json_in && bin_in) || (json_out && bin_out)) {
        throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_cdb);
    }

    const auto length = static_cast<size_t>(GetInt16(GetController()->GetCmd(), 5));
    if (!length) {
        throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_cdb);
    }

    GetController()->SetLength(length);
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

bool HostServices::ExecuteCommand(const CommandContext& context, PbResult& result)
{
    const PbCommand& command = context.GetCommand();
    const PbOperation operation = command.operation();

    if (!PbOperation_IsValid(operation)) {
        spdlog::trace("Ignored unknown command with operation opcode " + to_string(operation));

        return context.ReturnLocalizedError(LocalizationKey::ERROR_OPERATION, UNKNOWN_OPERATION, to_string(operation));
    }

    LogTrace("Received " + PbOperation_Name(operation) + " command");

    switch (operation) {
    case LOG_LEVEL:
        // TODO
        return context.ReturnErrorStatus("TODO");

    case DEFAULT_FOLDER:
        if (const string error = piscsi_image.SetDefaultFolder(protobuf_util::GetParam(command, "folder")); !error.empty()) {
            context.ReturnErrorStatus(error);
        }
        else {
            context.ReturnSuccessStatus();
        }
        break;

    case DEVICES_INFO:
        // TODO
        return context.ReturnErrorStatus("TODO");

    case DEVICE_TYPES_INFO:
        response.GetDeviceTypesInfo(*result.mutable_device_types_info());
        return context.WriteSuccessResult(result);

    case SERVER_INFO:
        // TODO
        return context.ReturnErrorStatus("TODO");

    case VERSION_INFO:
        response.GetVersionInfo(*result.mutable_version_info());
        return context.WriteSuccessResult(result);

    case LOG_LEVEL_INFO:
        response.GetLogLevelInfo(*result.mutable_log_level_info());
        return context.WriteSuccessResult(result);

    case DEFAULT_IMAGE_FILES_INFO:
        response.GetImageFilesInfo(*result.mutable_image_files_info(), piscsi_image.GetDefaultFolder(),
            protobuf_util::GetParam(command, "folder_pattern"),
            protobuf_util::GetParam(command, "file_pattern"), piscsi_image.GetDepth());
        return context.WriteSuccessResult(result);

    case IMAGE_FILE_INFO:
        if (string filename = protobuf_util::GetParam(command, "file"); filename.empty()) {
            context.ReturnLocalizedError(LocalizationKey::ERROR_MISSING_FILENAME);
        }
        else {
            auto image_file = make_unique<PbImageFile>();
            const bool status = response.GetImageFile(*image_file.get(), piscsi_image.GetDefaultFolder(),
                filename);
            if (status) {
                result.set_allocated_image_file_info(image_file.get());
                result.set_status(true);
                context.WriteResult(result);
            }
            else {
                context.ReturnLocalizedError(LocalizationKey::ERROR_IMAGE_FILE_INFO);
            }
        }
        break;

    case NETWORK_INTERFACES_INFO:
        response.GetNetworkInterfacesInfo(*result.mutable_network_interfaces_info());
        return context.WriteSuccessResult(result);

    case MAPPING_INFO:
        response.GetMappingInfo(*result.mutable_mapping_info());
        return context.WriteSuccessResult(result);

    case STATISTICS_INFO:
        // TODO
        return context.ReturnErrorStatus("TODO");

    case OPERATION_INFO:
        response.GetOperationInfo(*result.mutable_operation_info(), piscsi_image.GetDepth());
        return context.WriteSuccessResult(result);

    case RESERVED_IDS_INFO:
        // TODO
        return context.ReturnErrorStatus("TODO");

    case SHUT_DOWN:
        // TODO
        return context.ReturnErrorStatus("TODO");

    case NO_OPERATION:
        return context.ReturnSuccessStatus();

    case CREATE_IMAGE:
        return piscsi_image.CreateImage(context);

    case DELETE_IMAGE:
        return piscsi_image.DeleteImage(context);

    case RENAME_IMAGE:
        return piscsi_image.RenameImage(context);

    case COPY_IMAGE:
        return piscsi_image.CopyImage(context);

    case PROTECT_IMAGE:
        case UNPROTECT_IMAGE:
        return piscsi_image.SetImagePermissions(context);

    case RESERVE_IDS:
        // TODO
        return context.ReturnErrorStatus("TODO");

    default:
        // The remaining commands may only be executed when the target is idle
        // TODO
        return context.ReturnErrorStatus("TODO");
    }

    return true;
}

bool HostServices::WriteByteSequence(span<const uint8_t> buf)
{
    const auto length = static_cast<size_t>(GetInt16(GetController()->GetCmd(), 5));
    string cmd((const char *)buf.data(), length);

    PbCommand command;
    if (!JsonStringToMessage(cmd, &command).ok()) {
        throw scsi_exception(sense_key::illegal_request, asc::invalid_field_in_parameter_list);
    }

    CommandContext context(command, "", "");
    PbResult result;
    if (!ExecuteCommand(context, result)) {
        throw scsi_exception(sense_key::aborted_command);
    }

    string json;
    if (MessageToJsonString(result, &json).ok()) {
        const auto allocation_length = static_cast<size_t>(GetInt16(GetController()->GetCmd(), 7));

        memcpy(GetController()->GetBuffer().data(), json.c_str(), allocation_length);

        GetController()->SetLength(min(allocation_length, json.size()));

        EnterDataInPhase();
    }
    else {
        throw scsi_exception(sense_key::aborted_command);
    }

    return true;
}
