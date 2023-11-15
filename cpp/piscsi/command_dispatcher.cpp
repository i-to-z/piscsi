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

bool CommandDispatcher::DispatchCommand(const CommandContext& context)
{
	const PbCommand& command = context.GetCommand();
	const PbOperation operation = command.operation();

	// TODO
//	if (!access_token.empty() && access_token != GetParam(command, "token")) {
//		return context.ReturnLocalizedError(LocalizationKey::ERROR_AUTHENTICATION, UNAUTHORIZED);
//	}

	if (!PbOperation_IsValid(operation)) {
		spdlog::trace("Ignored unknown command with operation opcode " + to_string(operation));

		return context.ReturnLocalizedError(LocalizationKey::ERROR_OPERATION, UNKNOWN_OPERATION, to_string(operation));
	}

	spdlog::trace("Received " + PbOperation_Name(operation) + " command");

	PbResult result;

	switch(operation) {
		case LOG_LEVEL:
		    // TODO
//			if (const string log_level = GetParam(command, "level"); !SetLogLevel(log_level)) {
//				context.ReturnLocalizedError(LocalizationKey::ERROR_LOG_LEVEL, log_level);
//			}
//			else {
//				context.ReturnSuccessStatus();
//			}
			break;

		case DEFAULT_FOLDER:
		    // TODO
//			if (const string error = piscsi_image.SetDefaultFolder(GetParam(command, "folder")); !error.empty()) {
//				context.ReturnErrorStatus(error);
//			}
//			else {
//				context.ReturnSuccessStatus();
//			}
			break;

		case DEVICES_INFO:
			response.GetDevicesInfo(executor->GetAllDevices(), result, command, piscsi_image.GetDefaultFolder());
            return context.WriteSuccessResult(result);

		case DEVICE_TYPES_INFO:
			response.GetDeviceTypesInfo(*result.mutable_device_types_info());
			return context.WriteSuccessResult(result);

		case SERVER_INFO:
			response.GetServerInfo(*result.mutable_server_info(), command, executor->GetAllDevices(),
					executor->GetReservedIds(), piscsi_image.GetDefaultFolder(), piscsi_image.GetDepth());
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
				context.ReturnLocalizedError( LocalizationKey::ERROR_MISSING_FILENAME);
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
			response.GetStatisticsInfo(*result.mutable_statistics_info(), executor->GetAllDevices());
			return context.WriteSuccessResult(result);

		case OPERATION_INFO:
			response.GetOperationInfo(*result.mutable_operation_info(), piscsi_image.GetDepth());
			return context.WriteSuccessResult(result);

		case RESERVED_IDS_INFO:
			response.GetReservedIds(*result.mutable_reserved_ids_info(), executor->GetReservedIds());
			return context.WriteSuccessResult(result);

		case SHUT_DOWN:
		    // TODO
		    //return ShutDown(context, GetParam(command, "mode"));

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
			return executor->ProcessCmd(context);

		default:
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
	scoped_lock<mutex> lock(executor->GetExecutionLocker());
	return executor->ProcessCmd(context);
}

bool CommandDispatcher::HandleDeviceListChange(const CommandContext& context, PbOperation operation) const
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
