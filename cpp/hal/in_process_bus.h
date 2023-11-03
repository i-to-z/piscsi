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

// TODO Make access thread-safe

class InProcessBus : public GPIOBUS
{

public:

	InProcessBus() = default;
    ~InProcessBus() = default;

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

    bool enb = false;
    bool bsy = false;
    bool sel = false;
    bool atn = false;
    bool ack = false;
    bool act = false;
    bool rst = false;
    bool msg = false;
    bool cd = false;
    bool io = false;
    bool req = false;
    bool dp = false;
    uint8_t dat = 0;
};
