//---------------------------------------------------------------------------
//
//	SCSI Target Emulator PiSCSI
//	for Raspberry Pi
//
//	Powered by XM6 TypeG Technology.
//	Copyright (C) 2016-2020 GIMONS
//  Copyright (C) 2022 akuker
//
//	[ High resolution timer ]
//
//---------------------------------------------------------------------------

#pragma once

#include <memory>
#include <stdint.h>

class PlatformSpecificTimer
{

public:

    PlatformSpecificTimer() = default;
    virtual ~PlatformSpecificTimer() = default;

    virtual void Init() = 0;

    // Get system timer low byte
    virtual uint32_t GetTimerLow() = 0;
    // Get system timer high byte
    virtual uint32_t GetTimerHigh() = 0;
    // Sleep for N microseconds
    virtual void SleepUsec(uint32_t usec) = 0;
};

class SysTimer
{

public:

	static void Init();
    // Get system timer low byte
    static uint32_t GetTimerLow();
    // Get system timer high byte
    static uint32_t GetTimerHigh();
    // Sleep for N microseconds
    static void SleepUsec(uint32_t usec);

private:

    static std::unique_ptr<PlatformSpecificTimer> systimer_ptr;
};
