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
    const int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        spdlog::error("Unable to open /dev/mem");
        abort();
    }

    // Map peripheral region memory with the system timer addresses
    void *map = mmap(nullptr, 0x1000100, PROT_READ | PROT_WRITE, MAP_SHARED, fd, SBC_Version::GetPeripheralAddress());
    if (map == MAP_FAILED) {
        spdlog::error("Unable to map memory");
        close(fd);
        abort();
    }
    close(fd);

    // Save the base address
    systaddr = (uint32_t *)map + SYST_OFFSET / sizeof(uint32_t);

    // Change the ARM timer to free run mode
    auto armtaddr = static_cast<uint32_t *>(map) + ARMT_OFFSET / sizeof(uint32_t);
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
    const array<uint32_t, 32> maxclock = { 32, 0, 0x00030004, 8, 0, 4, 0, 0 };

    // Get the core frequency
    if (const int vcio_fd = open("/dev/vcio", O_RDONLY); vcio_fd >= 0) {
        ioctl(vcio_fd, _IOWR(100, 0, char *), maxclock.data());
        close(vcio_fd);
    }
}

// Timing is based on system timer low
void SysTimer::SleepUsec(uint32_t usec) const
{
    // If time is 0, don't do anything
    if (usec) {
    	const uint32_t now = systaddr[SYST_CLO];
    	while ((systaddr[SYST_CLO] - now) < usec) {
    		// Do nothing
    	}
	}
}
