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

#include "hal/systimer_raspberry.h"
#include "hal/sbc_version.h"

using namespace std;

unique_ptr<PlatformSpecificTimer> SysTimer::systimer_ptr;

void SysTimer::Init()
{
	if (SBC_Version::IsRaspberryPi()) {
		systimer_ptr = make_unique<SysTimer_Raspberry>();
	}
	systimer_ptr->Init();
}

uint32_t SysTimer::GetTimerLow()
{
    return systimer_ptr->GetTimerLow();
}

void SysTimer::SleepUsec(uint32_t usec)
{
    systimer_ptr->SleepUsec(usec);
}
