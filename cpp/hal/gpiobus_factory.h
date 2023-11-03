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

#include "hal/bus.h"
#include "hal/in_process_bus.h"
#include <memory>

class GPIOBUS_Factory
{

public:

	GPIOBUS_Factory() = default;
	~GPIOBUS_Factory() = default;

	static unique_ptr<BUS> Create(BUS::mode_e mode);

private:

	inline static InProcessBus in_process_busbus;
};
