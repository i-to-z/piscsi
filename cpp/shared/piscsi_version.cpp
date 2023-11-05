//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2020 akuker
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include <spdlog/spdlog.h>
#include "piscsi_version.h"

// The following should be updated for each release
const int piscsi_major_version = 23; // Last two digits of year
const int piscsi_minor_version = 10; // Month
const int piscsi_patch_version = -1; // Patch number - increment for each update

using namespace std;

string piscsi_get_version_string()
{
	return fmt::format("{0:02}{1:02}{2}.", piscsi_major_version, piscsi_minor_version,
			piscsi_patch_version < 0 ? " --DEVELOPMENT BUILD--" : "");
}


