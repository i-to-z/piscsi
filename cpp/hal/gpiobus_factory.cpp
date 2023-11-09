//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022 akuker
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "hal/gpiobus_factory.h"
#include "hal/gpiobus_raspberry.h"
#include "hal/sbc_version.h"
#include <spdlog/spdlog.h>

using namespace std;

unique_ptr<BUS> GPIOBUS_Factory::Create(BUS::mode_e mode, bool in_process)
{
    unique_ptr<BUS> bus;

    if (in_process) {
        bus = make_unique<DelegatingInProcessBus>(in_process_bus, true);
    }
    else {
        SBC_Version::Init();
        if (SBC_Version::IsRaspberryPi()) {
            if (getuid()) {
                spdlog::error("GPIO bus access requires root permissions");
                return nullptr;
            }

            bus = make_unique<GPIOBUS_Raspberry>();
        } else {
            bus = make_unique<DelegatingInProcessBus>(in_process_bus, false);
        }
    }

    if (bus && bus->Init(mode)) {
        bus->Reset();
    }

    return bus;
}
