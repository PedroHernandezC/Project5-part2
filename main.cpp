#include <iostream>

#include "HashTableDictionary.hpp"
#include<fstream>
#include<random>

#include <unordered_map>
#include <string>
#include <sstream>
#include <algorithm>
#include <filesystem>

#include "RunResults.hpp"
// #include "RunMetaData.hpp"
#include "Operations.hpp"

// The first line of the header must contain:  <profile> <N> <seed>
// After the header: blank lines and lines starting with '#' are okay
// and will be ignored.
// Opcodes: I <key>  | E <key

bool load_trace_strict_header(const std::string &path, RunMetaData &runMeta, std::vector<Operation> &out_operations) {
    std::string profile = "";
    int N = 0;
    int seed = 0;
    out_operations.clear();

    std::ifstream in(path);
    if (!in.is_open())
        return false;

    // --- read FIRST line as header
    std::string header;
    if (!std::getline(in, header))
        return false;

    // Look for a non-while-space character
    const auto first = header.find_first_not_of(" \t\r\n");
    // Since this is the first line, we don't expect it to be blank
    // or start with a comment.
    if (first == std::string::npos || header[first] == '#')
        return false;

    // Create a string stream so that we can read the profile name,
    // N, and the seed more easily.
    std::istringstream hdr(header);
    if (!(hdr >> profile >> N >> seed))
        return false;

    runMeta.profile = profile;
    runMeta.N = N;
    runMeta.seed = seed;

    // --- read ops, allowing comments/blank lines AFTER the header ---
    std::string line;
    while (std::getline(in, line)) {
        const auto opCodeIdx = line.find_first_not_of(" \t\r\n");
        if (opCodeIdx == std::string::npos || line[opCodeIdx] == '#')
            continue; // skip blank and comment lines.

        std::istringstream iss(line.substr(opCodeIdx));
        std::string tok; //
        if (!(iss >> tok))
            continue;

        std::string w1, w2; // there are two words after the op code.
                            // Maybe we should add a dash in between in the data file.
        if (tok == "I") {
            if (!(iss >> w1 >> w2)) return false;
            out_operations.emplace_back(OpCode::Insert, w1.append(" ") + w2);
        } else if (tok == "E") {
            if (!(iss >> w1 >> w2)) return false;
//            std::cout << "w1 = " << w1 << " w2 = " << w2 << std::endl;
            out_operations.emplace_back(OpCode::Erase, w1.append(" ") + w2);
        } else {
            std::cout << "Unknown operation in load_trace_strict_header: " << tok << std::endl;
            return false; // unknown token
        }
    }

    return true;
}

std::size_t tableSizeForN(std::size_t N) {
   static const std::vector<std::pair<std::size_t, std::size_t>> N_and_primes = {
        /* N = 2^10 = 1,024  */ { 1024,    1279    },
        /* N = 2^11 = 2,048  */ { 2048,    2551    },
        /* N = 2^12 = 4,096  */ { 4096,    5101    },
        /* N = 2^13 = 8,192  */ { 8192,   10273   },
        /* N = 2^14 = 16,384 */ { 16384,   20479   },
        /* N = 2^15 = 32,768 */ { 32768,   40849   },
        /* N = 2^16 = 65,536 */ { 65536,   81931   },
        /* N = 2^17 = 131,072*/ { 131072,  163861  },
        /* N = 2^18 = 262,144*/ { 262144,  327739  },
        /* N = 2^19 = 524,288*/ { 524288,  655243  },
        /* N = 2^20 = 1,048,576*/{ 1048576, 1310809 }
    };

    for (auto item: N_and_primes) {
        if (item.first == N)
            return item.second;
    }
    std::cout << "Unable to find table size for " << N << " in RunMetaData." << std::endl;
    exit(1);
}

void find_trace_files_or_die(const std::string &dir,
                             const std::string &profile_prefix,
                             std::vector<std::string> &out_files) {
    namespace fs = std::filesystem;
    out_files.clear();

    std::error_code ec;
    fs::path p(dir);

    if (!fs::exists(p, ec)) {
        std::cerr << "Error: directory '" << dir << "' does not exist";
        if (ec) std::cerr << " (" << ec.message() << ")";
        std::cerr << "\n";
        std::exit(1);
    }
    if (!fs::is_directory(p, ec)) {
        std::cerr << "Error: path '" << dir << "' is not a directory";
        if (ec) std::cerr << " (" << ec.message() << ")";
        std::cerr << "\n";
        std::exit(1);
    }

    fs::directory_iterator it(p, ec);
    if (ec) {
        std::cerr << "Error: cannot iterate directory '" << dir << "': "
                << ec.message() << "\n";
        std::exit(1);
    }

    const std::string suffix = ".trace";
    for (const auto &entry: it) {
        if (!entry.is_regular_file(ec)) {
            if (ec) {
                std::cerr << "Error: is_regular_file failed for '"
                        << entry.path().string() << "': " << ec.message() << "\n";
                std::exit(1);
            }
            continue;
        }

        const std::string name = entry.path().filename().string();
        const bool has_prefix = (name.rfind(profile_prefix, 0) == 0);
        const bool has_suffix = name.size() >= suffix.size() &&
                                name.compare(name.size() - suffix.size(),
                                             suffix.size(), suffix) == 0;

        if (has_prefix && has_suffix) {
            out_files.push_back(entry.path().string());
        }
    }

    std::sort(out_files.begin(), out_files.end()); // stable order for reproducibility
}

template<class Impl>
RunResult run_trace_ops(Impl &pq,
                        RunResult &runResult,
                        const std::vector<Operation> &ops) {
    // Count ops mostly for sanity check
    for (const auto &op: ops) {
        switch (op.tag) {
            case OpCode::Insert: ++runResult.inserts;
                break;
            case OpCode::Erase: ++runResult.erases;
                break;
        }
    }
    // One untimed run

    pq.clear();
    std::cout << "Starting the throw-away run for N = " << runResult.run_meta_data.N << std::endl;
    for (const auto &op: ops) {
        switch (op.tag) {
            case OpCode::Insert:
                pq.insert(op.key);
                break;
            case OpCode::Erase:
                (void) pq.remove(op.key);
                break;
        }
    }

    const int numTrials = 7;

    std::vector<std::int64_t> trials_ns;
    trials_ns.reserve(numTrials);

    using clock = std::chrono::steady_clock;
    for (int i = 0; i < numTrials; ++i) {
        pq.clear();
        std::cout << "Run " << i << " for N = " << runResult.run_meta_data.N << std::endl;
        auto t0 = clock::now();
        for (const auto &op: ops) {
            switch (op.tag) {
                case OpCode::Insert:
                    pq.insert(op.key);
                    break;
                case OpCode::Erase:
                    pq.remove(op.key);
                    break;
            }
        }
        auto t1 = clock::now();
        trials_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    const size_t mid = trials_ns.size() / 2;     // the median of 0..numTrials
    std::nth_element(trials_ns.begin(), trials_ns.begin()+mid, trials_ns.end());
    runResult.elapsed_ns = trials_ns[mid];

    return runResult;
}




int main() {
    const auto profileName = std::string("lru_profile");
    const auto traceDir = std::string("./traceFiles");
    const auto csvDir = std::string("./csvs");

    std::vector<std::string> traceFiles;

    find_trace_files_or_die(traceDir, profileName, traceFiles);
    if (traceFiles.size() == 0) {
        std::cerr << "No trace files found.\n";
        exit(1);
    }

    std::vector<RunResult> runResults;
    std::vector<std::string> hashCSV;
    for (auto traceFile: traceFiles) {
        const auto pos = traceFile.find_last_of("/\\");
        auto traceFileBaseName = (pos == std::string::npos) ? traceFile : traceFile.substr(pos + 1);

        std::vector<Operation> operations;
        RunMetaData run_meta_data;

        load_trace_strict_header(traceFile, run_meta_data, operations);
        
        std::vector<HashTableDictionary::PROBE_TYPE> probeTypes = {
            HashTableDictionary::DOUBLE,
            HashTableDictionary::SINGLE
        };

        for (auto pType : probeTypes) {
            RunResult oneRunResult(run_meta_data);

            auto doWePerformCompaction = true;
            HashTableDictionary hashDictionary(tableSizeForN(run_meta_data.N), pType, doWePerformCompaction);
            hashDictionary.clear();

            oneRunResult.impl = std::string("hash_map_") + ((pType == HashTableDictionary::SINGLE) ? "single" : "double");
            oneRunResult.trace_path = traceFileBaseName;
            run_trace_ops(hashDictionary, oneRunResult, operations);
            runResults.emplace_back(oneRunResult);

            std::cout << "in run trace printing csv.\n";
            std::cout << HashTableDictionary::csvStatsHeader() << std::endl;
            std::cout << hashDictionary.csvStats() << std::endl;
            std::cout << "in run trace printing csv ends.\n";


            hashDictionary.printMask();
            hashDictionary.printStats();
            hashCSV.emplace_back(hashDictionary.csvStats());
            if (doWePerformCompaction)
                hashDictionary.printBeforeAndAfterCompactionMaps();
            else
                hashDictionary.printActiveDeleteMap();
        }
    }

    if (runResults.size() == 0) {
        std::cerr << "No trace files found.\n";
        return 1;
    }

    const auto csvPath = csvDir + "/" + profileName + ".csv";
    std::ofstream csv_out(csvPath);

    if (!csv_out.is_open()) {
        std::cerr << "failed to open " << csvPath << " for writing.\n";
    } else {
        std::cout << RunResult::csv_header() << std::endl;
        csv_out << RunResult::csv_header() << std::endl;

        size_t index = 0;
        for (const auto& run : runResults) {
            std::cout << run.to_csv_row() + "," + hashCSV.at(index) << std::endl;
            csv_out << run.to_csv_row() + "," + hashCSV.at(index) << std::endl;
            index++;
        }
    }

    return 0;
}
