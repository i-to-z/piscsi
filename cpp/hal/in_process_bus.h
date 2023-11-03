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

class InProcessBus : public GPIOBUS
{

public:

	InProcessBus() = default;
    ~InProcessBus() override = default;

    bool Init(mode_e) override { return true; }

    void Reset() override {
    	enb = false;
    	bsy = false;
    	sel = false;
    	atn = false;
    	ack = false;
    	act = false;
    	rst = false;
    	msg = false;
    	cd = false;
    	io = false;
    	req = false;
    	dat = 0;
    }

    void Cleanup() override {
    	// Nothing to do
    }

    uint32_t Acquire() override { return dat; }

    void SetENB(bool) override { assert(false); }
    bool GetBSY() const override { return bsy; }
    void SetBSY(bool ast) override { bsy = ast; }
    bool GetSEL() const override { return sel; }
    void SetSEL(bool ast) override { sel = ast; }
    bool GetATN() const override { return atn; }
    void SetATN(bool ast) override { atn = ast; }
    bool GetACK() const override { return ack; }
    void SetACK(bool ast) override { act = ast; }
    bool GetACT() const override { return act; }
    void SetACT(bool ast) override { act = ast; }
    bool GetRST() const override { return rst; }
    void SetRST(bool ast) override { rst = ast; };
    bool GetMSG() const override { return msg; };
    void SetMSG(bool ast) override { msg = ast; };
    bool GetCD() const override { return cd; }
    void SetCD(bool ast) override { cd = ast; }
    bool GetIO() override { return io; }
    void SetIO(bool ast) override { io = ast; }
    bool GetREQ() const override { return req; }
    void SetREQ(bool ast) override { req = ast; }
    bool GetDP() const override {
    	assert(false);
    	return false;
    }

    bool WaitREQ(bool ast) override { return WaitSignal(PIN_REQ, ast); }

    bool WaitACK(bool ast) override { return WaitSignal(PIN_ACK, ast); }

    uint8_t GetDAT() override { return dat; }
    void SetDAT(uint8_t d) override { dat = d; }

private:

    void MakeTable() override { assert(false); }
    void SetControl(int, bool) override { assert(false); }
    void SetMode(int, int) override{ assert(false); }
    bool GetSignal(int) const override {
    	assert(false);
    	return false;
    }
    void SetSignal(int, bool) override { assert(false); };

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

    volatile bool enb = false; // NOSONAR volatile is fine here
    volatile bool bsy = false; // NOSONAR volatile is fine here
    volatile bool sel = false; // NOSONAR volatile is fine here
    volatile bool atn = false; // NOSONAR volatile is fine here
    volatile bool ack = false; // NOSONAR volatile is fine here
    volatile bool act = false; // NOSONAR volatile is fine here
    volatile bool rst = false; // NOSONAR volatile is fine here
    volatile bool msg = false; // NOSONAR volatile is fine here
    volatile bool cd = false; // NOSONAR volatile is fine here
    volatile bool io = false; // NOSONAR volatile is fine here
    volatile bool req = false; // NOSONAR volatile is fine here
    volatile uint8_t dat = 0; // NOSONAR volatile is fine here
};

// Required in order for the bus instances to be unique even though they must be shared between target and initiator
class DelegatingInProcessBus : public InProcessBus
{

public:

	explicit DelegatingInProcessBus(InProcessBus& b) : bus(b) {}
    ~DelegatingInProcessBus() = default;

    bool Init(mode_e mode) override { return bus.Init(mode); }

    void Reset() override { bus.Reset(); }

    void Cleanup() override { bus.Cleanup(); }

    uint32_t Acquire() override { return bus.Acquire(); }

    void SetENB(bool ast) override { bus.SetENB(ast); }
    bool GetBSY() const override { return bus.GetBSY(); }
    void SetBSY(bool ast) override { bus.SetBSY(ast); }
    bool GetSEL() const override { return bus.GetSEL(); }
    void SetSEL(bool ast) override { bus.SetSEL(ast); }
    bool GetATN() const override { return bus.GetATN(); }
    void SetATN(bool ast) override { bus.SetATN(ast); }
    bool GetACK() const override { return bus.GetACK(); }
    void SetACK(bool ast) override { bus.SetACK(ast); }
    bool GetACT() const override { return bus.GetACT(); }
    void SetACT(bool ast) override { bus.SetACT(ast); }
    bool GetRST() const override { return bus.GetRST(); }
    void SetRST(bool ast) override { bus.SetRST(ast); }
    bool GetMSG() const override { return bus.GetMSG(); }
    void SetMSG(bool ast) override { bus.SetMSG(ast); }
    bool GetCD() const override { return bus.GetCD(); }
    void SetCD(bool ast) override { bus.SetCD(ast); }
    bool GetIO() override { return bus.GetIO(); }
    void SetIO(bool ast) override { bus.SetIO(ast); }
    bool GetREQ() const override { return bus.GetREQ(); }
    void SetREQ(bool ast) override { bus.SetREQ(ast); }
    bool GetDP() const override { return bus.GetDP(); }

    bool WaitREQ(bool ast) override { return bus.WaitREQ(ast); }

    bool WaitACK(bool ast) override { return bus.WaitACK(ast); }

    uint8_t GetDAT() override { return bus.GetDAT(); }
    void SetDAT(uint8_t dat) override { bus.SetDAT(dat); }

    InProcessBus& bus;
};

