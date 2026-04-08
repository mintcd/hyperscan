#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "hs.h"
#include "grey.h"
#include "hs_internal.h"

using namespace std;

static void create_directories(const string& path) {
    string temp;
    for (size_t i = 0; i < path.length(); ++i) {
        temp += path[i];
        if (path[i] == '/' || path[i] == '\\') {
#ifdef _WIN32
            _mkdir(temp.c_str());
#else
            mkdir(temp.c_str(), 0777);
#endif
        }
    }
#ifdef _WIN32
    _mkdir(temp.c_str());
#else
    mkdir(temp.c_str(), 0777);
#endif
}

void dump_mode_info(const string& regex, unsigned int mode, const string& mode_name, const char *dump_path) {
    hs_database_t *db = nullptr;
    hs_compile_error_t *compile_err = nullptr;

    ue2::Grey grey;
    if (dump_path) {
        create_directories(dump_path);
        grey.dumpPath = dump_path;
        if (!grey.dumpPath.empty() && grey.dumpPath.back() != '/' && grey.dumpPath.back() != '\\') {
            grey.dumpPath.push_back('/');
        }
        grey.dumpFlags = ue2::Grey::DUMP_BASICS | ue2::Grey::DUMP_IMPL | 
                         ue2::Grey::DUMP_PARSE | ue2::Grey::DUMP_INT_GRAPH;
    }

    // Compiling with DOTALL and MULTILINE flags by default for maximum match exposure
    const char *exprs[] = { regex.c_str() };
    unsigned int flags[] = { HS_FLAG_DOTALL | HS_FLAG_MULTILINE };
    unsigned int ids[] = { 0 };

    hs_error_t err = ue2::hs_compile_multi_int(exprs, flags, ids, nullptr, 1,
                                               mode, nullptr, &db, &compile_err, grey);

    if (err != HS_SUCCESS) {
        cerr << "ERROR: Could not compile regex in " << mode_name << " mode." << endl;
        cerr << "Message: " << compile_err->message << endl;
        hs_free_compile_error(compile_err);
        return;
    }

    cout << "\n=== Mode: " << mode_name << " ===" << endl;

    char *info = nullptr;
    err = hs_database_info(db, &info);
    if (err == HS_SUCCESS && info) {
        cout << "Database Info:     " << info << endl;
        free(info);
    }

    size_t db_size = 0;
    if (hs_database_size(db, &db_size) == HS_SUCCESS) {
        cout << "Database Size:     " << db_size << " bytes" << endl;
    }

    size_t stream_size = 0;
    if (mode == HS_MODE_STREAM && hs_stream_size(db, &stream_size) == HS_SUCCESS) {
        cout << "Stream State Size: " << stream_size << " bytes" << endl;
    }

    hs_scratch_t *scratch = nullptr;
    if (hs_alloc_scratch(db, &scratch) == HS_SUCCESS && scratch) {
        size_t scratch_size = 0;
        if (hs_scratch_size(scratch, &scratch_size) == HS_SUCCESS) {
            cout << "Scratch Size:      " << scratch_size << " bytes" << endl;
        }
        hs_free_scratch(scratch);
    }

    hs_free_database(db);
}

int __cdecl main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        cerr << "Usage: " << argv[0] << " <regex> [dump_path]" << endl;
        cerr << "Example: " << argv[0] << " \"test.*string\" hs_dumps" << endl;
        return 1;
    }

    string regex = argv[1];
    const char *dump_path = (argc == 3) ? argv[2] : nullptr;
    cout << "Analyzing Regex: " << regex << endl;

    dump_mode_info(regex, HS_MODE_BLOCK, "BLOCK", dump_path);
    // dump_mode_info(regex, HS_MODE_STREAM, "STREAMING", dump_path);
    // dump_mode_info(regex, HS_MODE_VECTORED, "VECTORED", dump_path);

    // cout << "\n--- Internal Graph Dumps ---" << endl;
    // cout << "To get detailed Graphviz (.dot) dumps of the compiler's internal Rose/NFA/DFA engines:" << endl;
    // cout << "  1. Ensure Hyperscan was built with -DDUMP_SUPPORT=ON (default for Debug/RelWithDebInfo)." << endl;
    // cout << "  2. Run this tool with the HS_DUMP_PATH environment variable set." << endl;
    // cout << "\nExample:" << endl;
    // cout << "  mkdir /tmp/hs_dumps" << endl;
    // cout << "  HS_DUMP_PATH=/tmp/hs_dumps " << argv[0] << " \"" << regex << "\"" << endl;
    // cout << "  ls -l /tmp/hs_dumps" << endl;

    return 0;
}