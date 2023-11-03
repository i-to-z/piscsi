//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "hal/bus.h"
#include "phase_executor.h"
#include <spdlog/spdlog.h>
#include <ctime>
#include <iostream>
#include <array>
#include <string>
#include <chrono>

using namespace std;
using namespace spdlog;

void PhaseExecutor::Reset() const
{
	bus.SetDAT(0);
	bus.SetBSY(false);
	bus.SetSEL(false);
	bus.SetATN(false);
}

bool PhaseExecutor::Execute(scsi_command cmd, span<uint8_t> cdb, span<uint8_t> buffer, int length)
{
    spdlog::trace("Executing " + command_mapping.find(cmd)->second.second);

    if (!Arbitration()) {
		bus.Reset();
		return false;
    }

    if (!Selection()) {
		Reset();
		return false;
	}

    // Timeout 3 s
	auto now = chrono::steady_clock::now();
    while ((chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - now).count()) < 3) {
        bus.Acquire();

        if (bus.GetREQ()) {
        	try {
        		if (Dispatch(bus.GetPhase(), cmd, cdb, buffer, length)) {
        			now = chrono::steady_clock::now();
        		}
        		else {
        			bus.Reset();
         			return !GetStatus();
        		}
        	}
        	catch (const phase_exception& e) {
        		cerr << "Error: " << e.what() << endl;
        		bus.Reset();
        		return false;
        	}
        }
    }

    return false;
}

bool PhaseExecutor::Dispatch(phase_t phase, scsi_command cmd, span<uint8_t> cdb, span<uint8_t> buffer, int length)
{
	spdlog::trace(string("Handling ") + BUS::GetPhaseStrRaw(phase) + " phase");

	switch (phase) {
		case phase_t::command:
			Command(cmd, cdb);
			break;

		case phase_t::status:
			Status();
			break;

		case phase_t::datain:
			DataIn(buffer, length);
			break;

    	case phase_t::dataout:
    		DataOut(buffer, length);
    		break;

    	case phase_t::msgin:
    		MsgIn();
    		// Done with this command cycle
    		return false;

    	case phase_t::msgout:
    		MsgOut();
    		break;

    	default:
    		throw phase_exception(string("Ignoring ") + BUS::GetPhaseStrRaw(phase) + " phase");
    		break;
	}

    return true;
}

bool PhaseExecutor::Arbitration() const
{
	if (!WaitForFree()) {
		spdlog::trace("Bus is not free");
		return false;
	}

	nanosleep(&BUS_FREE_DELAY, nullptr);

	bus.SetDAT(static_cast<uint8_t>(1 << initiator_id));

	bus.SetBSY(true);

	nanosleep(&ARBITRATION_DELAY, nullptr);

	bus.Acquire();
	if (bus.GetDAT() > (1 << initiator_id)) {
		spdlog::trace("Lost ARBITRATION, competing initiator ID is " + to_string(bus.GetDAT() - (1 << initiator_id)));
		return false;
	}

	// TODO This should be in Selection() only, but then piscsi sometimes does not see the target ID
	bus.SetDAT(static_cast<uint8_t>(1 << (initiator_id + target_id)));

	bus.SetSEL(true);

	nanosleep(&BUS_CLEAR_DELAY, nullptr);
	nanosleep(&BUS_SETTLE_DELAY, nullptr);

	return true;
}

bool PhaseExecutor::Selection() const
{
	bus.SetDAT(static_cast<uint8_t>(1 << (initiator_id + target_id)));

    // Request MESSAGE OUT for IDENTIFY
    bus.SetATN(true);

	nanosleep(&DESKEW_DELAY, nullptr);
	nanosleep(&DESKEW_DELAY, nullptr);

    bus.SetBSY(false);

	nanosleep(&BUS_SETTLE_DELAY, nullptr);

    if (!WaitForBusy()) {
		spdlog::trace("SELECTION failed");
    	return false;
    }

	nanosleep(&DESKEW_DELAY, nullptr);
	nanosleep(&DESKEW_DELAY, nullptr);

    bus.SetSEL(false);

    return true;
}

void PhaseExecutor::Command(scsi_command cmd, span<uint8_t> cdb) const
{
    cdb[0] = static_cast<uint8_t>(cmd);
    if (target_lun < 8) {
    	// Encode LUN in the CDB for backwards compatibility with SCSI-1-CCS
    	cdb[1] = static_cast<uint8_t>(cdb[1] + (target_lun << 5));
    }

    if (static_cast<int>(cdb.size()) !=
        bus.SendHandShake(cdb.data(), static_cast<int>(cdb.size()), BUS::SEND_NO_DELAY)) {

        throw phase_exception(command_mapping.find(cmd)->second.second + string(" failed"));
    }
}

void PhaseExecutor::Status()
{
	array<uint8_t, 1> buf;

	if (bus.ReceiveHandShake(buf.data(), 1) != 1) {
        throw phase_exception("STATUS failed");
    }

	status = buf[0];
}

void PhaseExecutor::DataIn(span<uint8_t> buffer, int length)
{
    if (!bus.ReceiveHandShake(buffer.data(), length)) {
        throw phase_exception("DATA IN failed");
    }
}

void PhaseExecutor::DataOut(span<uint8_t> buffer, int length)
{
    if (!bus.SendHandShake(buffer.data(), length, BUS::SEND_NO_DELAY)) {
        throw phase_exception("DATA OUT failed");
    }
}

void PhaseExecutor::MsgIn() const
{
	array<uint8_t, 1> buf;

	if (bus.ReceiveHandShake(buf.data(), 1) != 1) {
        throw phase_exception("MESSAGE IN failed");
    }

	if (buf[0]) {
		throw phase_exception("MESSAGE IN did not report COMMAND COMPLETE");
	}
}

void PhaseExecutor::MsgOut() const
{
	array<uint8_t, 1> buf;

	// IDENTIFY
	buf[0] = static_cast<uint8_t>(target_lun | 0x80);

	if (bus.SendHandShake(buf.data(), buf.size(), BUS::SEND_NO_DELAY) != buf.size()) {
        throw phase_exception("MESSAGE OUT failed");
    }
}

bool PhaseExecutor::WaitForFree() const
{
    // Wait for up to 2 s
    int count = 10000;
    do {
        // Wait 20 ms
        const timespec ts = {.tv_sec = 0, .tv_nsec = 20 * 1000};
        nanosleep(&ts, nullptr);
        bus.Acquire();
        if (!bus.GetBSY() && !bus.GetSEL()) {
            return true;
        }
    } while (count--);

    return false;
}

bool PhaseExecutor::WaitForBusy() const
{
    // Wait for up to 2 s
    int count = 10000;
    do {
        // Wait 20 ms
        const timespec ts = {.tv_sec = 0, .tv_nsec = 20 * 1000};
        nanosleep(&ts, nullptr);
        bus.Acquire();
        if (bus.GetBSY()) {
            return true;
        }
    } while (count--);

    return false;
}

void PhaseExecutor::SetTarget(int id, int lun)
{
	target_id = id;
	target_lun = lun;
}
