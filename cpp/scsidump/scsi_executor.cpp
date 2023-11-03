//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "controllers/controller_manager.h"
#include "scsidump/scsi_executor.h"
#include <spdlog/spdlog.h>
#include <vector>

using namespace std;
using namespace spdlog;
using namespace scsi_defs;

ScsiExecutor::ScsiExecutor(BUS& bus, int id)
{
	phase_executor = make_unique<PhaseExecutor>(bus, id);
}

bool ScsiExecutor::TestUnitReady()
{
	vector<uint8_t> cdb(6);

    return phase_executor->Execute(scsi_command::eCmdTestUnitReady, cdb, {}, 0);
}

bool ScsiExecutor::Inquiry(span<uint8_t> buffer)
{
	vector<uint8_t> cdb(6);
	cdb[4] = 0xff;

    return phase_executor->Execute(scsi_command::eCmdInquiry, cdb, buffer, 256);
}

pair<uint64_t, uint32_t> ScsiExecutor::ReadCapacity()
{
	vector<uint8_t> buffer(14);
	vector<uint8_t> cdb(10);

    if (!phase_executor->Execute(scsi_command::eCmdReadCapacity10, cdb, buffer, 8)) {
    	return { 0, 0 };
    }

    uint64_t capacity = (static_cast<uint32_t>(buffer[0]) << 24) | (static_cast<uint32_t>(buffer[1]) << 16) |
    		(static_cast<uint32_t>(buffer[2]) << 8) | static_cast<uint32_t>(buffer[3]);

    int sector_size_offset = 4;

    if (static_cast<int32_t>(capacity) == -1) {
    	cdb.resize(16);
       	// READ CAPACITY(16), not READ LONG(16)
    	cdb[1] = 0x10;

    	if (!phase_executor->Execute(scsi_command::eCmdReadCapacity16_ReadLong16, cdb, buffer, 14)) {
        	return { 0, 0 };
    	}

    	capacity = (static_cast<uint64_t>(buffer[0]) << 56) | (static_cast<uint64_t>(buffer[1]) << 48) |
    			(static_cast<uint64_t>(buffer[2]) << 40) | (static_cast<uint64_t>(buffer[3]) << 32) |
				(static_cast<uint64_t>(buffer[4]) << 24) | (static_cast<uint64_t>(buffer[5]) << 16) |
				(static_cast<uint64_t>(buffer[6]) << 8) | static_cast<uint64_t>(buffer[7]);

    	sector_size_offset = 8;
    }

    const uint32_t sector_size = (static_cast<uint32_t>(buffer[sector_size_offset]) << 24) |
    		(static_cast<uint32_t>(buffer[sector_size_offset + 1]) << 16) |
			(static_cast<uint32_t>(buffer[sector_size_offset + 2]) << 8) |
			static_cast<uint32_t>(buffer[sector_size_offset + 3]);

    return { capacity + 1, sector_size };
}

bool ScsiExecutor::ReadWrite(span<uint8_t> buffer, uint32_t bstart, uint32_t blength, int length, bool isWrite)
{
	vector<uint8_t> cdb(10);
	cdb[2] = static_cast<uint8_t>(bstart >> 24);
    cdb[3] = static_cast<uint8_t>(bstart >> 16);
    cdb[4] = static_cast<uint8_t>(bstart >> 8);
    cdb[5] = static_cast<uint8_t>(bstart);
    cdb[7] = static_cast<uint8_t>(blength >> 8);
    cdb[8] = static_cast<uint8_t>(blength);

    return phase_executor->Execute(isWrite ? scsi_command::eCmdWrite10 : scsi_command::eCmdRead10, cdb, buffer, length);
}

void ScsiExecutor::SynchronizeCache()
{
	vector<uint8_t> cdb(10);

	phase_executor->Execute(scsi_command::eCmdSynchronizeCache10, cdb, {},  0);
}

set<int> ScsiExecutor::ReportLuns()
{
	vector<uint8_t> buffer(512);
	vector<uint8_t> cdb(12);
	cdb[8] = static_cast<uint8_t>(buffer.size() >> 8);
	cdb[9] = static_cast<uint8_t>(buffer.size());

	// Assume 8 LUNs in case REPORT LUNS is not available
	if (!phase_executor->Execute(scsi_command::eCmdReportLuns, cdb, buffer, buffer.size())) {
		spdlog::trace("Device does not support REPORT LUNS");
		return { 0, 1, 2, 3, 4, 5, 6, 7 };
	}

	const auto lun_count = (static_cast<size_t>(buffer[2]) << 8) | static_cast<size_t>(buffer[3]) / 8;
	spdlog::trace("Device reported LUN count of " + to_string(lun_count));

	set<int> luns;
	size_t offset = 8;
	for (size_t i = 0; i < lun_count && offset < buffer.size() - 8; i++, offset += 8) {
		const uint64_t lun =
				(static_cast<uint64_t>(buffer[offset]) << 56) | (static_cast<uint64_t>(buffer[offset + 1]) << 48) |
    			(static_cast<uint64_t>(buffer[offset + 1]) << 40) | (static_cast<uint64_t>(buffer[offset + 3]) << 32) |
				(static_cast<uint64_t>(buffer[offset + 4]) << 24) | (static_cast<uint64_t>(buffer[offset + 5]) << 16) |
				(static_cast<uint64_t>(buffer[offset + 6]) << 8) | static_cast<uint64_t>(buffer[offset + 7]);
		if (lun < static_cast<uint64_t>(ControllerManager::GetScsiLunMax())) {
			luns.insert(static_cast<int>(lun));
		}
		else {
			spdlog::trace("Device reported invalid LUN " + to_string(lun));
		}
	}

	return luns;
}
