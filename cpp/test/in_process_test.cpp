//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "shared/piscsi_util.h"
#include "piscsi/piscsi_core.h"
#include "scsidump/scsidump_core.h"
#include <thread>

using namespace std;
using namespace piscsi_util;

void add_arg(vector<char *>& args, const string& arg)
{
	args.push_back(strdup(arg.c_str()));
}

int main(int argc, char *argv[])
{
	string t_args;
	string i_args;

	int opt;
	while ((opt = getopt(argc, argv, "-i:t:")) != -1) {
		switch (opt) {
			case 'i':
				i_args = optarg;
				break;

			case 't':
				t_args = optarg;
				break;

			default:
				cerr << "Parser error" << endl;
				exit(EXIT_FAILURE);
				break;
		}
	}

	vector<char *> initiator_args;
	add_arg(initiator_args, "initiator");
	for (const auto& arg : Split(i_args, ' ')) {
		add_arg(initiator_args, arg);
	}

	vector<char *> target_args;
	add_arg(target_args, "target");
	for (const auto& arg : Split(t_args, ' ')) {
		add_arg(target_args, arg);
	}

	auto target_thread = jthread([&target_args] () {
		auto piscsi = make_unique<Piscsi>();
		piscsi->run(target_args, true);
	});

	// TODO Avoid sleep
	sleep(1);

	auto scsidump = make_unique<ScsiDump>();
	scsidump->run(initiator_args, true);

	exit(EXIT_SUCCESS);
}
