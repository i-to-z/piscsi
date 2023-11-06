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

#include "hal/systimer.h"
#include "hal/systimer_raspberry.h"
#include <spdlog/spdlog.h>

#include "hal/gpiobus.h"
#include "hal/sbc_version.h"

bool SysTimer::initialized = false;
bool SysTimer::is_raspberry = false;

using namespace std;

unique_ptr<PlatformSpecificTimer> SysTimer::systimer_ptr;

uint32_t SysTimer::GetTimerLow()
{
    return systimer_ptr->GetTimerLow();
}

uint32_t SysTimer::GetTimerHigh()
{
    return systimer_ptr->GetTimerHigh();
}

void SysTimer::SleepNsec(uint32_t nsec)
{
    systimer_ptr->SleepNsec(nsec);
}

void SysTimer::SleepUsec(uint32_t usec)
{
    systimer_ptr->SleepUsec(usec);
}
