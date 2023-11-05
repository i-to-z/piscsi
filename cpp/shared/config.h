//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Powered by XM6 TypeG Technology.
// Copyright (C) 2016-2020 GIMONS
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#pragma once

// Check SEL signal by event
#define USE_SEL_EVENT_ENABLE

// Currently for testing only: No BUS SETTLE DELAY in DATA IN/DATA OUT and COMMAND handshakes
// because these dealys are not covered by the SCSI specification.
// Also deals with other delays
#define NO_DELAY
