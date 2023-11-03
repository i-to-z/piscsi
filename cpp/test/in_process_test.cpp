//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "piscsi/piscsi_core.h"
#include "scsidump/scsidump_core.h"
#include <thread>

void add_arg(vector<char *>& args, const string& arg)
{
	args.push_back(strdup(arg.c_str()));
}

int main(int, char *[])
{
	vector<char *> piscsi_args;
	add_arg(piscsi_args, "piscsi");
	add_arg(piscsi_args, "-id");
	add_arg(piscsi_args, "0");
	add_arg(piscsi_args, "services");

	vector<char *> scsidump_args;
	add_arg(scsidump_args, "scsidump");
	add_arg(scsidump_args, "-I");
	add_arg(scsidump_args, "-t");
	add_arg(scsidump_args, "0");
	add_arg(scsidump_args, "-V");

	const auto mode = BUS::mode_e::IN_PROCESS;

	auto target_thread = jthread([&piscsi_args] () {
		auto piscsi = make_unique<Piscsi>();
		piscsi->run(piscsi_args, mode);
	});

	// TODO Avoid sleep
	sleep(1);

	auto initiator_thread = jthread([&scsidump_args] () {
		auto scsidump = make_unique<ScsiDump>();
		scsidump->run(scsidump_args, mode);
	});
}
