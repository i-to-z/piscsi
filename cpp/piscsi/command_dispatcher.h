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

	CommandDispatcher(shared_ptr<PiscsiExecutor> e) : executor(e) { }
	~CommandDispatcher() = default;

	bool DispatchCommand(const CommandContext&, PbResult&);

private:

	bool ExecuteWithLock(const CommandContext&);
	bool HandleDeviceListChange(const CommandContext&, PbOperation) const;

	shared_ptr<PiscsiExecutor> executor;

	PiscsiImage piscsi_image;

	PiscsiResponse response;
};
