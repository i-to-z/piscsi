//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "hal/in_process_bus.h"
#include <iostream>

void InProcessBus::Reset()
{
	// By initializing with all possible values the map becomes thread safe
	signals[PIN_BSY] = false;
	signals[PIN_SEL] = false;
	signals[PIN_ATN] = false;
	signals[PIN_ACK] = false;
	signals[PIN_ACT] = false;
	signals[PIN_RST] = false;
	signals[PIN_MSG] = false;
	signals[PIN_CD] = false;
	signals[PIN_IO] = false;
	signals[PIN_REQ] = false;

	dat = 0;
}

bool InProcessBus::GetSignal(int pin) const
{
	const auto& it = signals.find(pin);

	if (it == signals.end()) {
		cerr << "Error: Unhandled signal pin " << pin << endl;
		assert(false);
	}

	return it->second;
}