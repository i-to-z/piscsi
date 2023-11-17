//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022-2023 Uwe Seimet
//
// Host Services with support for realtime clock, shutdown and command execution
//
//---------------------------------------------------------------------------

#pragma once

#include "piscsi/command_context.h"
#include "piscsi/command_dispatcher.h"
#include "piscsi/piscsi_image.h"
#include "piscsi/piscsi_response.h"
#include "mode_page_device.h"
#include <span>
#include <vector>
#include <map>

using namespace std;
using namespace piscsi_interface;

class HostServices: public ModePageDevice
{
    inline static const int EXECUTE_BUFFER_SIZE = 65535;

public:

	explicit HostServices(int lun) : ModePageDevice(SCHS, lun) {}
	~HostServices() override = default;

	bool Init(const param_map&) override;

	vector<uint8_t> InquiryInternal() const override;
	void TestUnitReady() override;

	void SetDispatcher(shared_ptr<CommandDispatcher> d) { dispatcher = d; }

protected:

	void SetUpModePages(map<int, vector<byte>>&, int, bool) const override;

private:

    enum class protobuf_format {
        binary = 0b001,
        json = 0b010,
        text = 0b100
    };

	using mode_page_datetime = struct __attribute__((packed)) {
		// Major and minor version of this data structure (e.g. 1.0)
	    uint8_t major_version;
	    uint8_t minor_version;
	    // Current date and time, with daylight savings time adjustment applied
	    uint8_t year; // year - 1900
	    uint8_t month; // 0-11
	    uint8_t day; // 1-31
	    uint8_t hour; // 0-23
	    uint8_t minute; // 0-59
	    uint8_t second; // 0-59
	};

	void StartStopUnit() const;
    void ExecuteOperation();
    void ReceiveOperationResults();

    int ModeSense6(cdb_t, vector<uint8_t>&) const override;
	int ModeSense10(cdb_t, vector<uint8_t>&) const override;

	void AddRealtimeClockPage(map<int, vector<byte>>&, bool) const;

	bool WriteByteSequence(span<const uint8_t>) override;

	protobuf_format ConvertFormat() const;

	// Operation results per initiator
	unordered_map<int, string> execution_results;

	shared_ptr<CommandDispatcher> dispatcher;

	PiscsiImage piscsi_image;

    protobuf_format input_format = protobuf_format::binary;
};
