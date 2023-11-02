//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022-2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#pragma once

#include "hal/bus.h"
#include <memory>
#include <string>
#include <span>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <functional>

using namespace std;

class phase_exception : public runtime_error
{
	using runtime_error::runtime_error;
};

class ScsiDump
{

public:

    ScsiDump() = default;
    ~ScsiDump() = default;

    int run(const span<char *>);

    struct inquiry_info {
        string vendor;
        string product;
        string revision;
        uint32_t sector_size;
        uint64_t capacity;

        void GeneratePropertiesFile(const string&) const;
    };
    using inquiry_info_t = struct inquiry_info;

private:

    bool Banner(span<char *>) const;
    bool Init() const;
    void ParseArguments(span<char *>);
    void DisplayBoardId() const;
    void ScanBus();
    bool DisplayInquiry(inquiry_info&, bool);
    string DumpRestore();
    bool GetDeviceInfo(inquiry_info&);
    bool Execute(scsi_command, span<uint8_t>, int length);
    bool Dispatch(phase_t phase, scsi_command, span<uint8_t>, int);
    void TestUnitReady();
    bool Inquiry();
    pair<uint64_t, uint32_t> ReadCapacity();
    void ReadWrite(uint32_t, uint32_t, int, bool);
    void SynchronizeCache();
    vector<bool> ReportLuns();
    bool WaitForFree() const;
    bool WaitForBusy() const;
    void BusFreeDelay() const;
    void ArbitrationDelay() const;

    bool Arbitration() const;
	bool Selection() const;
	void Command(scsi_command, span<uint8_t>) const;
	void Status();
	void DataIn(int);
	void DataOut(int);
	void MsgIn() const;
	void MsgOut() const;

    static void CleanUp();
    static void TerminationHandler(int);

    // A static instance is needed because of the signal handler
    static inline unique_ptr<BUS> bus;

    vector<uint8_t> buffer;

    int target_id = -1;

    int target_lun = 0;

    int initiator_id = 7;

    int status = 0;

    string filename;

    bool inquiry = false;

    bool scan_bus = false;

    bool all_luns = false;

    bool restore = false;

    bool properties_file = false;

    static const int MINIMUM_BUFFER_SIZE = 1024 * 64;
    static const int DEFAULT_BUFFER_SIZE = 1024 * 1024;

    static inline const string DIVIDER = "----------------------------------------";

    static inline const unordered_map<byte, string> DEVICE_TYPES = {
    		{ byte{0}, "Direct Access" },
			{ byte{1}, "Sequential Access" },
			{ byte{2}, "Printer" },
			{ byte{3}, "Processor" },
			{ byte{4}, "Write-Once" },
			{ byte{5}, "CD-ROM/DVD/BD/DVD-RAM" },
			{ byte{6}, "Scanner" },
			{ byte{7}, "Optical Memory" },
			{ byte{8}, "Media Changer" },
			{ byte{9}, "Communications" },
			{ byte{10}, "Graphic Arts Pre-Press" },
			{ byte{11}, "Graphic Arts Pre-Press" },
			{ byte{12}, "Storage Array Controller" },
			{ byte{13}, "Enclosure Services" },
			{ byte{14}, "Simplified Direct Access" },
			{ byte{15}, "Optical Card Reader/Writer" },
			{ byte{16}, "Bridge Controller" },
			{ byte{17}, "Object-based Storage" },
			{ byte{18}, "Automation/Drive Interface" },
			{ byte{19}, "Security Manager" },
			{ byte{20}, "Host Managed Zoned Block" },
			{ byte{30}, "Well Known Logical Unit" }
    };

    inline static const long BUS_SETTLE_DELAY_NS = 400;
    inline static const timespec BUS_SETTLE_DELAY = {.tv_sec = 0, .tv_nsec = BUS_SETTLE_DELAY_NS};

    inline static const long BUS_CLEAR_DELAY_NS = 800;
    inline static const timespec BUS_CLEAR_DELAY = {.tv_sec = 0, .tv_nsec = BUS_CLEAR_DELAY_NS};

    inline static const long BUS_FREE_DELAY_NS = 800;
    inline static const timespec BUS_FREE_DELAY = {.tv_sec = 0, .tv_nsec = BUS_FREE_DELAY_NS};

    inline static const long DESKEW_DELAY_NS = 45;
    inline static const timespec DESKEW_DELAY = {.tv_sec = 0, .tv_nsec = DESKEW_DELAY_NS};

    inline static const long ARBITRATION_DELAY_NS = 2'400;
    inline static const timespec ARBITRATION_DELAY = {.tv_sec = 0, .tv_nsec = ARBITRATION_DELAY_NS};

//+==============================-===================================+
//|  Timing description          |      Timing value                 |
//|------------------------------+-----------------------------------|
//|  Arbitration delay           |     2,4 us                        |
//|  Assertion period            |      90 ns                        |
//|  Bus clear delay             |     800 ns                        |
//|  Bus free delay              |     800 ns                        |
//|  Bus set delay               |     1,8 us                        |
//|  Bus settle delay            |     400 ns                        |
//|  Cable skew delay            |      10 ns                        |
//|  Data release delay          |     400 ns                        |
//|  Deskew delay                |      45 ns                        |
//|  Disconnection delay         |     200 us                        |
//|  Hold time                   |      45 ns                        |
//|  Negation period             |      90 ns                        |
//|  Power-on to selection time  |      10 s recommended             |
//|  Reset to selection time     |     250 ms recommended            |
//|  Reset hold time             |      25 us                        |
//|  Selection abort time        |     200 us                        |
//|  Selection time-out delay    |     250 ms recommended            |
//|  Transfer period             |     set during an SDTR message    |
//|  Fast assertion period       |      30 ns                        |
//|  Fast cable skew delay       |       5 ns                        |
//|  Fast deskew delay           |      20 ns                        |
//|  Fast hold time              |      10 ns                        |
//|  Fast negation period        |      30 ns                        |
//+==================================================================+
};
