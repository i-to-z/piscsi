//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "hal/in_process_bus.h"
#include <spdlog/spdlog.h>

void InProcessBus::Reset()
{
	spdlog::trace("Resetting bus");

	signals = {};

	dat = 0;
}

bool InProcessBus::GetSignal(int pin) const
{
	return signals[pin];
}

void InProcessBus::SetSignal(int pin, bool state)
{
	scoped_lock<mutex> lock(write_locker);
	signals[pin] = state;
}

bool InProcessBus::WaitSignal(int pin, bool state)
{
	const auto now = chrono::steady_clock::now();

    do {
        if (signals[PIN_RST]) {
            return false;
        }

        if (signals[pin] == state) {
            return true;
        }
    } while ((chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - now).count()) < 3);

    return false;
}

bool DelegatingInProcessBus::Init(mode_e mode)
{
	assert(mode == mode_e::IN_PROCESS_TARGET || mode == mode_e::IN_PROCESS_INITIATOR);

	in_process_mode = mode;

	return bus.Init(mode);
}

bool DelegatingInProcessBus::GetSignal(int pin) const
{
	const bool state = bus.GetSignal(pin);

	spdlog::trace(GetMode() + ": Getting " + GetSignalName(pin) + (state ? ": true" : ": false"));

	return state;
}

void DelegatingInProcessBus::SetSignal(int pin, bool state)
{
	spdlog::trace(GetMode() + ": Setting " + GetSignalName(pin) + " to " + (state ? "true" : "false"));

	bus.SetSignal(pin, state);
}

bool DelegatingInProcessBus::WaitSignal(int pin, bool state)
{
	spdlog::trace(GetMode() + ": Waiting for " + GetSignalName(pin) + " to become " + (state ? "true" : "false"));

	return bus.WaitSignal(pin, state);
}

string DelegatingInProcessBus::GetSignalName(int pin) const
{
	const auto& it = SIGNALS.find(pin);
	return it != SIGNALS.end() ? it->second : "????";
}
