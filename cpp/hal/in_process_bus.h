//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#pragma once

#include "hal/gpiobus.h"
#include <cstdint>
#include <cassert>
#include <unordered_map>
#include <mutex>
#include <atomic>

class InProcessBus : public GPIOBUS
{

public:

	InProcessBus() = default;
	~InProcessBus() override = default;

    void Reset() override;

    void Cleanup() override {
    	// Nothing to do
    }

    uint32_t Acquire() override { return dat; }

    bool GetBSY() const override { return GetSignal(PIN_BSY); }
    void SetBSY(bool state) override { SetSignal(PIN_BSY, state); }
    bool GetSEL() const override { return GetSignal(PIN_SEL); }
    void SetSEL(bool state) override { SetSignal(PIN_SEL, state);}
    bool GetATN() const override { return GetSignal(PIN_ATN); }
    void SetATN(bool state) override { SetSignal(PIN_ATN, state); }
    bool GetACK() const override { return GetSignal(PIN_ACK); }
    void SetACK(bool state) override { SetSignal(PIN_ACK, state); }
    bool GetRST() const override { return GetSignal(PIN_RST); }
    void SetRST(bool state) override { SetSignal(PIN_RST, state); };
    bool GetMSG() const override { return GetSignal(PIN_MSG); };
    void SetMSG(bool state) override { SetSignal(PIN_MSG, state);};
    bool GetCD() const override { return GetSignal(PIN_CD); }
    void SetCD(bool state) override { SetSignal(PIN_CD, state);}
    bool GetIO() override { return GetSignal(PIN_IO); }
    void SetIO(bool state) override { SetSignal(PIN_IO, state); }
    bool GetREQ() const override { return GetSignal(PIN_REQ); }
    void SetREQ(bool state) override { SetSignal(PIN_REQ, state); }
    bool GetDP() const override {
    	assert(false);
    	return false;
    }

    bool WaitSignal(int, bool) override;

    bool WaitREQ(bool state) override { return WaitSignal(PIN_REQ, state); }

    bool WaitACK(bool state) override { return WaitSignal(PIN_ACK, state); }

    uint8_t GetDAT() override { return dat; }
    void SetDAT(uint8_t d) override { dat = d; }

    bool GetSignal(int pin) const override;
    void SetSignal(int, bool) override;

    bool WaitForSelectEvent() override;

private:

    void MakeTable() override { assert(false); }
    void SetControl(int, bool) override { assert(false); }
    void SetMode(int, int) override{ assert(false); }

    void DisableIRQ() override {
    	// Nothing to do
    }
    void EnableIRQ() override {
    	// Nothing to do }
    }

    void PinConfig(int, int) override {
    	// Nothing to do
    }
    void PullConfig(int, int) override {
    	// Nothing to do
    }
    void PinSetSignal(int, bool) override { assert(false); }
    void DrvConfig(uint32_t) override {
    	// Nothing to do
    }

    // TODO This method should not exist at all, it pollutes the bus interface
    unique_ptr<DataSample> GetSample(uint64_t) override { assert(false); return nullptr; }

    mutex write_locker;

    mutex sel_mutex;
    condition_variable sel_condition;

    atomic<uint8_t> dat = 0;

    array<bool, 28> signals;
};

// Required in order for the bus instances to be unique even though they must be shared between target and initiator
class DelegatingInProcessBus : public InProcessBus
{

public:

	DelegatingInProcessBus(InProcessBus& b, bool l) : bus(b), log_signals(l) {}
    ~DelegatingInProcessBus() override = default;

    bool Init(mode_e) override;

    void Reset() override;

    void Cleanup() override { bus.Cleanup(); }

    uint32_t Acquire() override { return bus.Acquire(); }

    bool WaitREQ(bool state) override { return bus.WaitSignal(PIN_REQ, state); }

    bool WaitACK(bool state) override { return bus.WaitSignal(PIN_ACK, state); }

    uint8_t GetDAT() override { return bus.GetDAT(); }
    void SetDAT(uint8_t dat) override { bus.SetDAT(dat); }

    bool GetSignal(int) const override;
    void SetSignal(int, bool) override;
    bool WaitSignal(int, bool) override;

    bool IsTarget() const override { return in_process_mode == mode_e::IN_PROCESS_TARGET; }

private:

    string GetMode() const { return in_process_mode == mode_e::IN_PROCESS_TARGET ? "target" :"initiator"; }

    string GetSignalName(int) const;

    InProcessBus& bus;

    bool log_signals = true;

    mode_e in_process_mode = mode_e::IN_PROCESS_TARGET;

    inline static const unordered_map<int, string> SIGNALS {
       	{ PIN_BSY, "BSY" },
       	{ PIN_SEL, "SEL" },
       	{ PIN_ATN, "ATN" },
       	{ PIN_ACK, "ACK" },
       	{ PIN_RST, "RST" },
       	{ PIN_MSG, "MSG" },
       	{ PIN_CD, "CD" },
       	{ PIN_IO, "IO" },
    	{ PIN_REQ, "REQ" }
    };
};
