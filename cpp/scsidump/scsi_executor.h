//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <set>
#include <span>
#include "scsidump/phase_executor.h"

using namespace std;

class ScsiExecutor
{

public:

	ScsiExecutor(BUS& bus, int id) { phase_executor = make_unique<PhaseExecutor>(bus, id); }
    ~ScsiExecutor() = default;

    bool TestUnitReady();
    bool Inquiry(span<uint8_t>);
    pair<uint64_t, uint32_t> ReadCapacity();
    bool ReadWrite(span<uint8_t>, uint32_t, uint32_t, int, bool);
    void SynchronizeCache();
    set<int> ReportLuns();

    void SetTarget(int id, int lun) { phase_executor->SetTarget(id, lun); }

private:

    unique_ptr<PhaseExecutor> phase_executor;
};
