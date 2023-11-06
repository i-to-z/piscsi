//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022 akuker
//
//---------------------------------------------------------------------------

#include "mocks.h"
#include "hal/gpiobus_raspberry.h"
#include "test/test_shared.h"

extern "C" {
uint32_t get_dt_ranges(const char *filename, uint32_t offset);
uint32_t bcm_host_get_peripheral_address();
}

TEST(GpiobusRaspberry, GetDtRanges)
{
    const string soc_ranges_file = "/proc/device-tree/soc/ranges";

    vector<uint8_t> data;
    // If bytes 4-7 are non-zero, get_peripheral address should return those bytes
    data = vector<uint8_t>(
        {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF});
    CreateTempFileWithData(soc_ranges_file, data);
    EXPECT_EQ(0x44556677, GPIOBUS_Raspberry::bcm_host_get_peripheral_address());
    DeleteTempFile("/proc/device-tree/soc/ranges");

    // If bytes 4-7 are zero, get_peripheral address should return bytes 8-11
    data = vector<uint8_t>(
        {0x00, 0x11, 0x22, 0x33, 0x00, 0x00, 0x00, 0x00, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF});
    CreateTempFileWithData(soc_ranges_file, data);
    EXPECT_EQ(0x8899AABB, GPIOBUS_Raspberry::bcm_host_get_peripheral_address());
    DeleteTempFile("/proc/device-tree/soc/ranges");

    // If bytes 4-7 are zero, and 8-11 are 0xFF, get_peripheral address should return a default address of 0x20000000
    data = vector<uint8_t>(
        {0x00, 0x11, 0x22, 0x33, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xCC, 0xDD, 0xEE, 0xFF});
    CreateTempFileWithData(soc_ranges_file, data);
    EXPECT_EQ(0x20000000, GPIOBUS_Raspberry::bcm_host_get_peripheral_address());
    DeleteTempFile("/proc/device-tree/soc/ranges");

    remove_all(test_data_temp_path);
}
