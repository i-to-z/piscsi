//---------------------------------------------------------------------------
//
// SCSI Target Emulator PiSCSI
// for Raspberry Pi
//
// Copyright (C) 2023 Uwe Seimet
//
//---------------------------------------------------------------------------

#include "shared/scsi.h"
#include "scsiexec/scsi_executor.h"
#include "generated/piscsi_interface.pb.h"
#include <google/protobuf/util/json_util.h>
#include <spdlog/spdlog.h>
#include <array>
#include <fstream>
#include <iostream>
#include <filesystem>

using namespace std;
using namespace filesystem;
using namespace spdlog;
using namespace scsi_defs;
using namespace piscsi_interface;

bool ScsiExecutor::Execute(const string& input_filename, const string& output_filename, bool binary, string& result)
{
    int input_length = 0;

    if (!binary) {
        ifstream in(input_filename);
        if (in.fail()) {
            result = "Can't open JSON input file '" + input_filename + "': " + strerror(errno);
            return false;
        }

        stringstream buf;
        buf << in.rdbuf();
        const string json = buf.str();
        input_length = json.size();
        memcpy(buffer.data(), json.data(), input_length);
    }
    else {
        ifstream in(input_filename, ios::binary);
        if (in.fail()) {
            result = "Can't open binary input file '" + input_filename + "': " + strerror(errno);
            return false;
        }

        input_length = file_size(input_filename);
        vector<char> b(input_length);
        in.read(b.data(), input_length);
        memcpy(buffer.data(), b.data(), input_length);
    }

    array<uint8_t, 10> cdb = { };
    cdb[1] = binary ? 0x0a : 0x05;
    cdb[5] = static_cast<uint8_t>(input_length >> 8);
    cdb[6] = static_cast<uint8_t>(input_length);
    cdb[7] = static_cast<uint8_t>(buffer.size() >> 8);
    cdb[8] = static_cast<uint8_t>(buffer.size());

    phase_executor->Execute(scsi_command::eCmdExecute, cdb, buffer, input_length, buffer.size());

    const int length = phase_executor->GetByteCount();

    if (binary) {
        PbResult r;
        if (!r.ParseFromArray(buffer.data(), length)) {
            result = "Can't parse received binary protobuf data";
            return false;
        }

        if (output_filename.empty()) {
            google::protobuf::util::MessageToJsonString(r, &result);
        }
        else {
            if (binary) {
                 ofstream out(output_filename, ios::binary);
                 if (out.fail()) {
                     result = "Can't open binary output file '" + output_filename + "'";
                     return false;
                 }
            }
            else {
                ofstream out(output_filename);
                if (out.fail()) {
                    result = "Can't open JSON output file '" + output_filename + "'";
                    return false;
                }
            }
        }
    }
    else {
        const string json((const char*) buffer.data(), length);

        if (output_filename.empty()) {
            result = json;
        }
        else {

        }
    }

    return true;
}

bool ScsiExecutor::ShutDown()
{
    array<uint8_t, 6> cdb = { };
    cdb[4] = 0x02;

    phase_executor->Execute(scsi_command::eCmdStartStop, cdb, buffer, 0, 0);

    return true;
}
