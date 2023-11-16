//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022-2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#pragma once

#include "piscsi/command_context.h"
#include "piscsi/piscsi_executor.h"
#include "piscsi/piscsi_image.h"
#include "piscsi/piscsi_response.h"
#include "generated/piscsi_interface.pb.h"

using namespace std;

class CommandDispatcher
{

public:

	CommandDispatcher(PiscsiImage& i, PiscsiResponse& r, PiscsiExecutor& e)
	    : piscsi_image(i), response(r), executor(e) { }
	~CommandDispatcher() = default;

	bool DispatchCommand(const CommandContext&, PbResult&);

	bool ShutDown(AbstractController::piscsi_shutdown_mode) const;

	static bool SetLogLevel(const string&);

private:

	bool ExecuteWithLock(const CommandContext&);
	bool HandleDeviceListChange(const CommandContext&, PbOperation) const;
	bool ShutDown(const CommandContext&, const string&);

	PiscsiImage& piscsi_image;

	PiscsiResponse& response;

    PiscsiExecutor& executor;
};
