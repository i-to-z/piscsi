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
#include "hal/gpiobus.h"
#include "hal/sbc_version.h"
#include <spdlog/spdlog.h>
#include <memory>
#include <sys/ioctl.h>
#include <sys/mman.h>

using namespace std;

SysTimer::SysTimer()
{
    const int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd == -1) {
        spdlog::error("Error: Unable to open /dev/mem. Are you running as root?");
        return;
    }

    // Map peripheral region memory
    void *map = mmap(nullptr, 0x1000100, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, SBC_Version::GetPeripheralAddress());
    if (map == MAP_FAILED) {
        spdlog::error("Error: Unable to map memory");
        close(mem_fd);
        return;
    }
    close(mem_fd);

    // Save the base address
    systaddr = (uint32_t *)map + SYST_OFFSET / sizeof(uint32_t);

    // Change the ARM timer to free run mode
    uint32_t *armtaddr = (uint32_t *)map + ARMT_OFFSET / sizeof(uint32_t);
    armtaddr[ARMT_CTRL] = 0x00000282;

    // RPI Mailbox property interface
    // Get max clock rate
    //  Tag: 0x00030004
    //
    //  Request: Length: 4
    //   Value: u32: clock id
    //  Response: Length: 8
    //   Value: u32: clock id, u32: rate (in Hz)
    //
    // Clock id
    //  0x000000004: CORE
    const array<uint32_t, 32> maxclock = {32, 0, 0x00030004, 8, 0, 4, 0, 0};

    // Get the core frequency
    if (const int vcio_fd = open("/dev/vcio", O_RDONLY); vcio_fd >= 0) {
        ioctl(vcio_fd, _IOWR(100, 0, char *), maxclock.data());
        close(vcio_fd);
    }
}

// Timing is based on system timer low
void SysTimer::SleepUsec(uint32_t usec)
{
    // If time is 0, don't do anything
    if (usec) {
    	const uint32_t now = systaddr[SYST_CLO];
    	while ((systaddr[SYST_CLO] - now) < usec) {
    		// Do nothing
    	}
	}
}
