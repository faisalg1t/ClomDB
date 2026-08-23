// Simple interactive/one-shot CLI for ClomDB.
//
//   clomdb_cli <db_path> put <key> <value>
//   clomdb_cli <db_path> get <key>
//   clomdb_cli <db_path> delete <key>
//   clomdb_cli <db_path> scan
//   clomdb_cli <db_path> stats
//   clomdb_cli <db_path>            (no extra args: interactive REPL)
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "clomdb/clomdb.h"

using clomdb::DB;
using clomdb::Options;
using clomdb::ReadOptions;
using clomdb::Status;
using clomdb::WriteOptions;

namespace {

int RunOne(DB* db, const std::vector<std::string>& args) {
    if (args.empty()) return 0;
    const std::string& cmd = args[0];

    if (cmd == "put" && args.size() == 3) {
        Status s = db->Put(WriteOptions(), args[1], args[2]);
        if (!s.ok()) {
            std::cerr << "error: " << s.ToString() << "\n";
            return 1;
        }
        std::cout << "OK\n";
    } else if (cmd == "get" && args.size() == 2) {
        std::string value;
        Status s = db->Get(ReadOptions(), args[1], &value);
        if (s.IsNotFound()) {
            std::cout << "(not found)\n";
        } else if (!s.ok()) {
            std::cerr << "error: " << s.ToString() << "\n";
            return 1;
        } else {
            std::cout << value << "\n";
        }
    } else if ((cmd == "delete" || cmd == "del") && args.size() == 2) {
        Status s = db->Delete(WriteOptions(), args[1]);
        if (!s.ok()) {
            std::cerr << "error: " << s.ToString() << "\n";
            return 1;
        }
        std::cout << "OK\n";
    } else if (cmd == "scan" && args.size() == 1) {
        auto it = db->NewIterator(ReadOptions());
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            std::cout << it->key().ToString() << " = " << it->value().ToString() << "\n";
        }
    } else if (cmd == "flush" && args.size() == 1) {
        Status s = db->Flush();
        if (!s.ok()) {
            std::cerr << "error: " << s.ToString() << "\n";
            return 1;
        }
        std::cout << "OK\n";
    } else if (cmd == "stats" && args.size() == 1) {
        auto stats = db->GetStats();
        std::cout << "memtable: " << stats.memtable_entries << " entries, "
                  << stats.memtable_bytes << " bytes\n";
        for (int lvl = 0; lvl < clomdb::kMaxLevels; lvl++) {
            if (stats.files_per_level[lvl] == 0) continue;
            std::cout << "L" << lvl << ": " << stats.files_per_level[lvl] << " files, "
                      << stats.bytes_per_level[lvl] << " bytes\n";
        }
    } else if (cmd == "help") {
        std::cout << "commands: put <k> <v> | get <k> | delete <k> | scan | flush | stats | help | exit\n";
    } else {
        std::cerr << "unrecognized command (try 'help'): " << cmd << "\n";
        return 1;
    }
    return 0;
}

std::vector<std::string> SplitLine(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <db_path> [command args...]\n";
        return 2;
    }
    std::string db_path = argv[1];

    Options options;
    DB* db = nullptr;
    Status s = DB::Open(options, db_path, &db);
    if (!s.ok()) {
        std::cerr << "failed to open db: " << s.ToString() << "\n";
        return 1;
    }

    int rc = 0;
    if (argc > 2) {
        std::vector<std::string> args(argv + 2, argv + argc);
        rc = RunOne(db, args);
    } else {
        std::cout << "ClomDB CLI -- database at " << db_path << " ('help' for commands, 'exit' to quit)\n";
        std::string line;
        while (true) {
            std::cout << "clomdb> " << std::flush;
            if (!std::getline(std::cin, line)) break;
            auto args = SplitLine(line);
            if (args.empty()) continue;
            if (args[0] == "exit" || args[0] == "quit") break;
            RunOne(db, args);
        }
    }

    delete db;
    return rc;
}
