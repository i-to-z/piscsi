//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022-2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "piscsi/command_dispatcher.h"
#include "shared/piscsi_util.h"
#include "shared/protobuf_util.h"
#include "shared/piscsi_exceptions.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace spdlog;
using namespace piscsi_interface;
using namespace piscsi_util;
using namespace protobuf_util;

bool CommandDispatcher::DispatchCommand(const CommandContext& context, PbResult& result, const string& device)
{
	const PbCommand& command = context.GetCommand();
	const PbOperation operation = command.operation();

	if (!PbOperation_IsValid(operation)) {
		spdlog::trace("Ignored unknown command with operation opcode " + to_string(operation));

		return context.ReturnLocalizedError(LocalizationKey::ERROR_OPERATION, UNKNOWN_OPERATION, to_string(operation));
	}

	spdlog::trace(device + " Received " + PbOperation_Name(operation) + " command");

    switch (operation) {
    case LOG_LEVEL:
        if (const string log_level = GetParam(command, "level"); !SetLogLevel(log_level)) {
            return context.ReturnLocalizedError(LocalizationKey::ERROR_LOG_LEVEL, log_level);
        }
        else {
            return context.ReturnSuccessStatus();
        }

    case DEFAULT_FOLDER:
        if (const string error = piscsi_image.SetDefaultFolder(GetParam(command, "folder")); !error.empty()) {
            context.WriteResult(result);
            return false;
        }
        else {
            return context.WriteSuccessResult(result);
        }

    case DEVICES_INFO:
        response.GetDevicesInfo(executor.GetAllDevices(), result, command, piscsi_image.GetDefaultFolder());
        return context.WriteSuccessResult(result);

    case DEVICE_TYPES_INFO:
        response.GetDeviceTypesInfo(*result.mutable_device_types_info());
        return context.WriteSuccessResult(result);

    case SERVER_INFO:
        response.GetServerInfo(*result.mutable_server_info(), command, executor.GetAllDevices(),
            executor.GetReservedIds(), piscsi_image.GetDefaultFolder(), piscsi_image.GetDepth());
        return context.WriteSuccessResult(result);

    case VERSION_INFO:
        response.GetVersionInfo(*result.mutable_version_info());
        return context.WriteSuccessResult(result);

    case LOG_LEVEL_INFO:
        response.GetLogLevelInfo(*result.mutable_log_level_info());
        return context.WriteSuccessResult(result);

    case DEFAULT_IMAGE_FILES_INFO:
        response.GetImageFilesInfo(*result.mutable_image_files_info(), piscsi_image.GetDefaultFolder(),
            GetParam(command, "folder_pattern"), GetParam(command, "file_pattern"), piscsi_image.GetDepth());
        return context.WriteSuccessResult(result);

    case IMAGE_FILE_INFO:
        if (string filename = GetParam(command, "file"); filename.empty()) {
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
        response.GetStatisticsInfo(*result.mutable_statistics_info(), executor.GetAllDevices());
        return context.WriteSuccessResult(result);

    case OPERATION_INFO:
        response.GetOperationInfo(*result.mutable_operation_info(), piscsi_image.GetDepth());
        return context.WriteSuccessResult(result);

    case RESERVED_IDS_INFO:
        response.GetReservedIds(*result.mutable_reserved_ids_info(), executor.GetReservedIds());
        return context.WriteSuccessResult(result);

    case SHUT_DOWN:
        return ShutDown(context, GetParam(command, "mode"));

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
        return executor.ProcessCmd(context);

    default:
        // TODO Verify, especially for host services device
        // The remaining commands may only be executed when the target is idle
        if (!ExecuteWithLock(context)) {
            return false;
        }

        return HandleDeviceListChange(context, operation);
    }

    return true;
}

bool CommandDispatcher::ExecuteWithLock(const CommandContext& context)
{
	scoped_lock<mutex> lock(executor.GetExecutionLocker());
	return executor.ProcessCmd(context);
}

bool CommandDispatcher::HandleDeviceListChange(const CommandContext& context, PbOperation operation) const
{
	// ATTACH and DETACH return the resulting device list
	if (operation == ATTACH || operation == DETACH) {
		// A command with an empty device list is required here in order to return data for all devices
		PbCommand command;
		PbResult result;
		response.GetDevicesInfo(executor.GetAllDevices(), result, command, piscsi_image.GetDefaultFolder());
		context.WriteResult(result);
		return result.status();
	}

	return true;
}

// Shutdown on a remote interface command
bool CommandDispatcher::ShutDown(const CommandContext& context, const string& m) const
{
    if (m.empty()) {
        return context.ReturnLocalizedError(LocalizationKey::ERROR_SHUTDOWN_MODE_MISSING);
    }

    AbstractController::piscsi_shutdown_mode mode = AbstractController::piscsi_shutdown_mode::NONE;
    if (m == "rascsi") {
        mode = AbstractController::piscsi_shutdown_mode::STOP_PISCSI;
    }
    else if (m == "system") {
        mode = AbstractController::piscsi_shutdown_mode::STOP_PI;
    }
    else if (m == "reboot") {
        mode = AbstractController::piscsi_shutdown_mode::RESTART_PI;
    }
    else {
        return context.ReturnLocalizedError(LocalizationKey::ERROR_SHUTDOWN_MODE_INVALID, m);
    }

    // Shutdown modes other than rascsi require root permissions
    if (mode != AbstractController::piscsi_shutdown_mode::STOP_PISCSI && getuid()) {
        return context.ReturnLocalizedError(LocalizationKey::ERROR_SHUTDOWN_PERMISSION);
    }

    // Report success now because after a shutdown nothing can be reported anymore
    PbResult result;
    context.WriteSuccessResult(result);

    return ShutDown(mode);
}

// Shutdown on a SCSI command
bool CommandDispatcher::ShutDown(AbstractController::piscsi_shutdown_mode shutdown_mode) const
{
    switch(shutdown_mode) {
    case AbstractController::piscsi_shutdown_mode::STOP_PISCSI:
        spdlog::info("PiSCSI shutdown requested");
        return true;

    case AbstractController::piscsi_shutdown_mode::STOP_PI:
        spdlog::info("Raspberry Pi shutdown requested");
        if (system("init 0") == -1) {
            spdlog::error("Raspberry Pi shutdown failed");
        }
        break;

    case AbstractController::piscsi_shutdown_mode::RESTART_PI:
        spdlog::info("Raspberry Pi restart requested");
        if (system("init 6") == -1) {
            spdlog::error("Raspberry Pi restart failed");
        }
        break;

    case AbstractController::piscsi_shutdown_mode::NONE:
        assert(false);
        break;
    }

    return false;
}

bool CommandDispatcher::SetLogLevel(const string& log_level)
{
    int id = -1;
    int lun = -1;
    string level = log_level;

    if (const auto& components = Split(log_level, COMPONENT_SEPARATOR, 2); !components.empty()) {
        level = components[0];

        if (components.size() > 1) {
            if (const string error = ProcessId(components[1], id, lun); !error.empty()) {
                spdlog::warn("Error setting log level: " + error);
                return false;
            }
        }
    }

    const level::level_enum l = level::from_str(level);
    // Compensate for spdlog using 'off' for unknown levels
    if (to_string_view(l) != level) {
        spdlog::warn("Invalid log level '" + level + "'");
        return false;
    }

    set_level(l);
    DeviceLogger::SetLogIdAndLun(id, lun);

    if (id != -1) {
        if (lun == -1) {
            spdlog::info("Set log level for device " + to_string(id) + " to '" + level + "'");
        }
        else {
            spdlog::info("Set log level for device " + to_string(id) + ":" + to_string(lun) + " to '" + level + "'");
        }
    }
    else {
        spdlog::info("Set log level to '" + level + "'");
    }

    return true;
}
