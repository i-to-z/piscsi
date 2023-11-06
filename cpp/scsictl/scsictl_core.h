//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2022-2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#pragma once

#include <vector>

using namespace std;

class ScsiCtl
{

public:

	int run(const vector<char *>&) const;

private:

	void Banner(const vector<char *>&) const;
};
