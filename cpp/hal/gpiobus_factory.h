//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022 akuker
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#pragma once

#include "hal/in_process_bus.h"
#include <memory>

class GPIOBUS_Factory
{

public:

	static unique_ptr<BUS> Create(BUS::mode_e, bool = false);

private:

	// Bus instance shared by initiator and target
	inline static InProcessBus in_process_bus;
};
