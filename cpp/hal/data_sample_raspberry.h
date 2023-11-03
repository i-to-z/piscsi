//---------------------------------------------------------------------------
//
//	SCSI Target Emulator PiSCSI
//	for Raspberry Pi
//
//	Copyright (C) 2022 akuker
//
//	[ Logical representation of a single data sample ]
//
//---------------------------------------------------------------------------

#pragma once

#include "hal/data_sample.h"
#include "shared/scsi.h"

#if defined CONNECT_TYPE_STANDARD
#include "hal/connection_type/connection_standard.h"
#elif defined CONNECT_TYPE_FULLSPEC
#include "hal/connection_type/connection_fullspec.h"
#else
#error Invalid connection type or none specified
#endif

class DataSample_Raspberry final : public DataSample
{
  public:
    bool GetSignal(int pin) const override
    {
        return (bool)((data >> pin) & 1);
    };

    bool GetBSY() const override
    {
        return GetSignal(PIN_BSY);
    }
    bool GetSEL() const override
    {
        return GetSignal(PIN_SEL);
    }
    bool GetATN() const override
    {
        return GetSignal(PIN_ATN);
    }
    bool GetACK() const override
    {
        return GetSignal(PIN_ACK);
    }
    bool GetRST() const override
    {
        return GetSignal(PIN_RST);
    }
    bool GetMSG() const override
    {
        return GetSignal(PIN_MSG);
    }
    bool GetCD() const override
    {
        return GetSignal(PIN_CD);
    }
    bool GetIO() const override
    {
        return GetSignal(PIN_IO);
    }
    bool GetREQ() const override
    {
        return GetSignal(PIN_REQ);
    }
    bool GetACT() const override
    {
        return GetSignal(PIN_ACT);
    }
    bool GetDP() const override
    {
        return GetSignal(PIN_DP);
    }
    uint8_t GetDAT() const override
    {
        return (data >> (PIN_DT0 - 0)) & 0x01
        		+ (data >> (PIN_DT1 - 1)) & 0x02
				+ (data >> (PIN_DT2 - 2)) & 0x04
				+ (data >> (PIN_DT3 - 3)) & 0x08
				+ (data >> (PIN_DT4 - 4)) & 0x10
				+ (data >> (PIN_DT5 - 5)) & 0x20
				+ (data >> (PIN_DT6 - 6)) & 0x40
				+ (data >> (PIN_DT7 - 7)) & 0x80;
    }

    uint32_t GetRawCapture() const override
    {
        return data;
    }

    DataSample_Raspberry(const uint32_t in_data, const uint64_t in_timestamp) : DataSample{in_timestamp}, data{in_data}
    {
    }
    DataSample_Raspberry() = default;

    ~DataSample_Raspberry() override = default;

  private:
    uint32_t data = 0;
};
