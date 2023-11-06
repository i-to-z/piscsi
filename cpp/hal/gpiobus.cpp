//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Powered by XM6 TypeG Technology.
// Copyright (C) 2016-2020 GIMONS
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "hal/gpiobus.h"
#include "hal/systimer.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#ifdef __linux__
#include <sys/epoll.h>
#endif
#include <chrono>

using namespace std;

bool GPIOBUS::Init(mode_e mode)
{
    operation_mode = mode;

    return true;
}

int GPIOBUS::CommandHandShake(vector<uint8_t>& buf)
{
	assert(IsTarget());

    DisableIRQ();

    SetREQ(ON);

    bool ret = WaitACK(ON);

#ifndef NO_DELAY
    // Wait until the signal line stabilizes
    SysTimer::SleepNsec(SCSI_DELAY_BUS_SETTLE_DELAY_NS);
#endif

    // Get data
    buf[0] = GetDAT();

    SetREQ(OFF);

    // Timeout waiting for ACK assertion
    if (!ret) {
        EnableIRQ();
        return 0;
    }

    ret = WaitACK(OFF);

    // Timeout waiting for ACK to clear
    if (!ret) {
        EnableIRQ();
        return 0;
    }

    // The ICD AdSCSI ST, AdSCSI Plus ST and AdSCSI Micro ST host adapters allow SCSI devices to be connected
    // to the ACSI bus of Atari ST/TT computers and some clones. ICD-aware drivers prepend a $1F byte in front
    // of the CDB (effectively resulting in a custom SCSI command) in order to get access to the full SCSI
    // command set. Native ACSI is limited to the low SCSI command classes with command bytes < $20.
    // Most other host adapters (e.g. LINK96/97 and the one by Inventronik) and also several devices (e.g.
    // UltraSatan or GigaFile) that can directly be connected to the Atari's ACSI port also support ICD
    // semantics. I fact, these semantics have become a standard in the Atari world.

    // PiSCSI becomes ICD compatible by ignoring the prepended $1F byte before processing the CDB.
    if (buf[0] == 0x1F) {
        SetREQ(ON);

        ret = WaitACK(ON);

#ifndef NO_DELAY
        SysTimer::SleepNsec(SCSI_DELAY_BUS_SETTLE_DELAY_NS);
#endif
        // Get the actual SCSI command
        buf[0] = GetDAT();

        SetREQ(OFF);

        if (!ret) {
            EnableIRQ();
            return 0;
        }

        WaitACK(OFF);

        if (!ret) {
            EnableIRQ();
            return 0;
        }
    }

    const int command_byte_count = GetCommandByteCount(buf[0]);
    if (command_byte_count == 0) {
        EnableIRQ();

        // Unknown command
        return 0;
    }

    int offset = 0;

    int bytes_received;
    for (bytes_received = 1; bytes_received < command_byte_count; bytes_received++) {
        ++offset;

        SetREQ(ON);

        ret = WaitACK(ON);

#ifndef NO_DELAY
        // Wait until the signal line stabilizes
        SysTimer::SleepNsec(SCSI_DELAY_BUS_SETTLE_DELAY_NS);
#endif

        buf[offset] = GetDAT();

        SetREQ(OFF);

        // Check for timeout waiting for ACK assertion
        if (!ret) {
            break;
        }

        ret = WaitACK(OFF);

        // Check for timeout waiting for ACK to clear
        if (!ret) {
            break;
        }
    }

    EnableIRQ();

    return bytes_received;
}

//---------------------------------------------------------------------------
//
//	Handshake for DATA IN and MESSAGE IN
//
//---------------------------------------------------------------------------
int GPIOBUS::ReceiveHandShake(uint8_t *buf, int count)
{
	int i;

    DisableIRQ();

    if (IsTarget()) {
        for (i = 0; i < count; i++) {
            SetREQ(ON);

            bool ret = WaitACK(ON);

#ifndef NO_DELAY
            // Wait until the signal line stabilizes
            SysTimer::SleepNsec(SCSI_DELAY_BUS_SETTLE_DELAY_NS);
#endif

            *buf = GetDAT();

            SetREQ(OFF);

            // Check for timeout waiting for ACK signal
            if (!ret) {
                break;
            }

            ret = WaitACK(OFF);

            // Check for timeout waiting for ACK to clear
            if (!ret) {
                break;
            }

            buf++;
        }
    } else {
        // Get phase
        Acquire();
        phase_t phase = GetPhase();

        for (i = 0; i < count; i++) {
            // Wait for the REQ signal to be asserted
            bool ret = WaitREQ(ON);

            // Check for timeout waiting for REQ signal
            if (!ret) {
                break;
            }

// Assumption: Phase does not change here, but only below
#ifndef NO_DELAY
            // Phase error
            Acquire();
            if (GetPhase() != phase) {
                break;
            }
#endif

#ifndef NO_DELAY
            // Wait until the signal line stabilizes
            SysTimer::SleepNsec(SCSI_DELAY_BUS_SETTLE_DELAY_NS);
#endif

            *buf = GetDAT();

            SetACK(ON);

            ret = WaitREQ(OFF);

            SetACK(OFF);

            // Check for timeout waiting for REQ to clear
            if (!ret) {
                break;
            }

            // Phase error
            Acquire();
            if (GetPhase() != phase) {
                break;
            }

            buf++;
        }
    }

    EnableIRQ();

    // Return the number of bytes received
    return i;
}

//---------------------------------------------------------------------------
//
//	Handshake for DATA OUT and MESSAGE OUT
//
//---------------------------------------------------------------------------
int GPIOBUS::SendHandShake(uint8_t *buf, int count, int daynaport_delay_after_bytes)
{
	int i;

    DisableIRQ();

    if (IsTarget()) {
        for (i = 0; i < count; i++) {
           	// TODO Try to get rid of this
        	if (i == daynaport_delay_after_bytes) {
                 SysTimer::SleepUsec(SCSI_DELAY_SEND_DATA_DAYNAPORT_US);
            }

            SetDAT(*buf);

            bool ret = WaitACK(OFF);

            // Check for timeout waiting for ACK to clear
            if (!ret) {
                break;
            }

            SetREQ(ON);

            ret = WaitACK(ON);

            SetREQ(OFF);

            // Check for timeout waiting for ACK to clear
            if (!ret) {
                break;
            }

            buf++;
        }

        WaitACK(OFF);
    } else {
        Acquire();
        phase_t phase = GetPhase();

        for (i = 0; i < count; i++) {
            // Set the DATA signals
            SetDAT(*buf);

            // Wait for REQ to be asserted
            bool ret = WaitREQ(ON);

            // Check for timeout waiting for REQ to be asserted
            if (!ret) {
                break;
            }

           	// Signal the last MESSAGE OUT byte
            if (phase == phase_t::msgout && i == count - 1) {
            	SetATN(false);
            }

// Assumption: Phase does not change here, but only below
#ifndef NO_DELAY
            // Phase error
            Acquire();
            if (GetPhase() != phase) {
                break;
            }
#endif

            SetACK(ON);

            ret = WaitREQ(OFF);

            SetACK(OFF);

            // Check for timeout waiting for REQ to clear
            if (!ret) {
                break;
            }

            // Phase error
            Acquire();
            if (GetPhase() != phase) {
                break;
            }

            buf++;
        }
    }

    EnableIRQ();

    // Return number of transmissions
    return i;
}

bool GPIOBUS::WaitForSelectEvent()
{
#ifndef USE_SEL_EVENT_ENABLE
	Acquire();
	if (!GetSEL()) {
		const timespec ts = { .tv_sec = 0, .tv_nsec = 0};
		nanosleep(&ts, nullptr);
		return false;
	}
#else
    errno = 0;

    if (epoll_event epev; epoll_wait(epfd, &epev, 1, -1) <= 0) {
        spdlog::warn("epoll_wait failed");
        return false;
    }

    if (gpioevent_data gpev; read(selevreq.fd, &gpev, sizeof(gpev)) < 0) {
        spdlog::warn("read failed");
        return false;
    }

    Acquire();
#endif

    return true;
}

bool GPIOBUS::WaitSignal(int pin, bool ast)
{
	const auto now = chrono::steady_clock::now();

    // Wait 3 s
    do {
        Acquire();

        if (GetSignal(pin) == ast) {
            return true;
        }

        // Abort on a reset
        if (GetRST()) {
            return false;
        }
    } while ((chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - now).count()) < 3);

    return false;
}
