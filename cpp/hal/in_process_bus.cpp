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
	// By initializing with all possible values the map becomes thread safe
	signals[PIN_BSY] = make_pair(false, "BSY");
	signals[PIN_SEL] = make_pair(false, "SEL");
	signals[PIN_ATN] = make_pair(false, "ATN");
	signals[PIN_ACK] = make_pair(false, "ACK");
	signals[PIN_ACT] = make_pair(false, "ACT");
	signals[PIN_RST] = make_pair(false, "RST");
	signals[PIN_MSG] = make_pair(false, "MSG");
	signals[PIN_CD] = make_pair(false, "CD");
	signals[PIN_IO] = make_pair(false, "IO");
	signals[PIN_REQ] = make_pair(false, "REQ");

	dat = 0;
}

bool InProcessBus::GetSignal(int pin) const
{
	const auto& signal = FindSignal(pin);

	// Prevent excessive logging
	if (pin != PIN_SEL) {
		spdlog::trace("Getting " + signal.second);
	}

	return signal.first;
}

void InProcessBus::SetSignal(int pin, bool ast)
{
	const auto signal = FindSignal(pin);

	spdlog::trace("Setting " + signal.second + " to " + (ast ? "true" : "false"));

	signals[pin].first = ast;
}

bool InProcessBus::WaitSignal(int pin, bool ast)
{
	spdlog::trace("Waiting for " + FindSignal(pin).second + " to become " + (ast ? "true" : "false"));

	return GPIOBUS::WaitSignal(pin, ast);
}

pair<bool, string> InProcessBus::FindSignal(int pin) const
{
	const auto& it = signals.find(pin);
	if (it == signals.end()) {
		spdlog::critical("Unhandled signal pin " + to_string(pin));
		assert(false);
	}

	return it->second;
}
