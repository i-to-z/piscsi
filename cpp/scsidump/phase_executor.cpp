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
#include <array>
#include <string>

using namespace std;

void PhaseExecutor::Reset(int id, int lun)
{
	target_id = id;
	target_lun = lun;

	status = -1;
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

	// TODO Remove this code block, it should be in Selection() only, but then piscsi sometimes does not see the target ID
	auto data = static_cast<byte>(1 << initiator_id);
	data |= static_cast<byte>(1 << target_id);
	bus.SetDAT(static_cast<uint8_t>(data));

	bus.SetSEL(true);

	nanosleep(&BUS_CLEAR_DELAY, nullptr);
	nanosleep(&BUS_SETTLE_DELAY, nullptr);

	return true;
}

bool PhaseExecutor::Selection() const
{
	auto data = static_cast<byte>(1 << initiator_id);
	data |= static_cast<byte>(1 << target_id);
	bus.SetDAT(static_cast<uint8_t>(data));

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
    cdb[1] = static_cast<uint8_t>(static_cast<byte>(cdb[1]) | static_cast<byte>(target_lun << 5));
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
