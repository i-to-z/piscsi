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

    void SetENB(bool) override { assert(false); }
    bool GetBSY() const override { return GetSignal(PIN_BSY); }
    void SetBSY(bool state) override { SetSignal(PIN_BSY, state); }
    bool GetSEL() const override { return GetSignal(PIN_SEL); }
    void SetSEL(bool state) override { SetSignal(PIN_SEL, state);}
    bool GetATN() const override { return GetSignal(PIN_ATN); }
    void SetATN(bool state) override { SetSignal(PIN_ATN, state); }
    bool GetACK() const override { return GetSignal(PIN_ACK); }
    void SetACK(bool state) override { SetSignal(PIN_ACK, state); }
    bool GetACT() const override { return GetSignal(PIN_ACT); }
    void SetACT(bool state) override {SetSignal(PIN_ACT, state); }
    bool GetRST() const override { return GetSignal(PIN_RST); }
    void SetRST(bool state) override {SetSignal(PIN_RST, state); };
    bool GetMSG() const override { return GetSignal(PIN_MSG); };
    void SetMSG(bool state) override { SetSignal(PIN_MSG, state);};
    bool GetCD() const override { return GetSignal(PIN_CD); }
    void SetCD(bool state) override { SetSignal(PIN_CD, state);}
    bool GetIO() override { return GetSignal(PIN_IO); }
    void SetIO(bool state) override {SetSignal(PIN_IO, state); }
    bool GetREQ() const override { return GetSignal(PIN_REQ); }
    void SetREQ(bool state) override {SetSignal(PIN_REQ, state); }
    bool GetDP() const override {
    	assert(false);
    	return false;
    }

    bool WaitSignal(int, bool);

    pair<bool, string> FindSignal(int) const;

    bool WaitREQ(bool state) override { return WaitSignal(PIN_REQ, state); }

    bool WaitACK(bool state) override { return WaitSignal(PIN_ACK, state); }

    uint8_t GetDAT() override { return dat; }
    void SetDAT(uint8_t d) override { dat = d; }

protected:

    string GetMode() const { return IsTarget() ? "target" :"initiator"; }

private:

    void MakeTable() override { assert(false); }
    void SetControl(int, bool) override { assert(false); }
    void SetMode(int, int) override{ assert(false); }
    bool GetSignal(int pin) const override;
    void SetSignal(int, bool) override;

    void DisableIRQ() override {
    	// Nothing to do
    }
    void EnableIRQ() override {
    	// Nothing to do }
    }

    void PinConfig(int , int) override {
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

    unordered_map<int, pair<atomic_bool, string>> signals;

    atomic<uint8_t> dat = 0;
};

// Required in order for the bus instances to be unique even though they must be shared between target and initiator
class DelegatingInProcessBus : public InProcessBus
{

public:

	explicit DelegatingInProcessBus(InProcessBus& b) : bus(b) {}
    ~DelegatingInProcessBus() override = default;

    bool Init(mode_e) override;

    void Reset() override { bus.Reset(); }

    void Cleanup() override { bus.Cleanup(); }

    uint32_t Acquire() override { return bus.Acquire(); }

    void SetENB(bool state) override { bus.SetENB(state); }
    bool GetBSY() const override { return bus.GetBSY(); }
    void SetBSY(bool state) override { bus.SetBSY(state); }
    bool GetSEL() const override { return bus.GetSEL(); }
    void SetSEL(bool state) override { bus.SetSEL(state); }
    bool GetATN() const override { return bus.GetATN(); }
    void SetATN(bool state) override { bus.SetATN(state); }
    bool GetACK() const override { return bus.GetACK(); }
    void SetACK(bool state) override { bus.SetACK(state); }
    bool GetACT() const override { return bus.GetACT(); }
    void SetACT(bool state) override { bus.SetACT(state); }
    bool GetRST() const override { return bus.GetRST(); }
    void SetRST(bool state) override { bus.SetRST(state); }
    bool GetMSG() const override { return bus.GetMSG(); }
    void SetMSG(bool state) override { bus.SetMSG(state); }
    bool GetCD() const override { return bus.GetCD(); }
    void SetCD(bool state) override { bus.SetCD(state); }
    bool GetIO() override { return bus.GetIO(); }
    void SetIO(bool state) override { bus.SetIO(state); }
    bool GetREQ() const override { return bus.GetREQ(); }
    void SetREQ(bool state) override { bus.SetREQ(state); }
    bool GetDP() const override { return bus.GetDP(); }

    bool WaitREQ(bool state) override { return bus.WaitREQ(state); }

    bool WaitACK(bool state) override { return bus.WaitACK(state); }

    uint8_t GetDAT() override { return bus.GetDAT(); }
    void SetDAT(uint8_t dat) override { bus.SetDAT(dat); }

    bool IsTarget() const override { return in_process_mode == mode_e::IN_PROCESS_TARGET; }

    InProcessBus& bus;

    mode_e in_process_mode = mode_e::IN_PROCESS_TARGET;
};

