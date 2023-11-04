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

int main(int argc, char *argv[])
{
	vector<char *> piscsi_args;
	add_arg(piscsi_args, "piscsi");
	add_arg(piscsi_args, "-id");
	add_arg(piscsi_args, "0");
	add_arg(piscsi_args, "-t");
	add_arg(piscsi_args, "scrm");
	add_arg(piscsi_args, "/home/us/hatari/zip.hds");
	add_arg(piscsi_args, "-id");
	add_arg(piscsi_args, "6");
	add_arg(piscsi_args, "services");

	vector<char *> scsidump_args;
	add_arg(scsidump_args, "scsidump");
	add_arg(scsidump_args, "-f");
	add_arg(scsidump_args, "/tmp/a");
	add_arg(scsidump_args, "-t");
	add_arg(scsidump_args, "0");

    int opt;
	while ((opt = getopt(argc, argv, "-v")) != -1) {
		switch (opt) {
			case 'v':
				// Setting the log level is also effective for the in-process scsidump
				add_arg(piscsi_args, "-L");
				add_arg(piscsi_args, "trace");
				break;

			default:
				cerr << "Parser error" << endl;
				exit(EXIT_FAILURE);
				break;
		}
	}

	auto target_thread = jthread([&piscsi_args] () {
		auto piscsi = make_unique<Piscsi>();
		piscsi->run(piscsi_args, BUS::mode_e::IN_PROCESS_TARGET);
	});

	// TODO Avoid sleep
	sleep(1);

	auto scsidump = make_unique<ScsiDump>();
	scsidump->run(scsidump_args, BUS::mode_e::IN_PROCESS_INITIATOR);

	exit(EXIT_SUCCESS);
}
