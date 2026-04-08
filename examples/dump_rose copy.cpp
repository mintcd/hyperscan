#include "config.h"

#include "compiler/compiler.h"
#include "database.h"
#include "fdr/fdr_confirm.h"
#include "fdr/fdr_compile_internal.h"
#include "fdr/fdr_engine_description.h"
#include "fdr/fdr_internal.h"
#include "fdr/teddy_engine_description.h"
#include "grey.h"
#include "hwlm/hwlm_internal.h"
#include "hwlm/noodle_internal.h"
#include "hs_compile.h"
#include "nfagraph/ng.h"
#include "rose/rose_build_impl.h"
#include "rose/rose_internal.h"
#include "rose/rose_program.h"
#include "util/graph_range.h"
#include "util/target_info.h"

#include <cctype>
#include <exception>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#undef min
#undef max
#endif

using namespace std;

namespace {

struct FaCountSummary {
    size_t total = 0;
    size_t graph = 0;
    size_t dfa = 0;
    size_t haig = 0;
    size_t castle = 0;
    size_t tamarama = 0;
    size_t mpv = 0;
};

static void addUniquePtr(const void *p, set<const void *> *seen,
                         size_t *field, size_t *total) {
    if (!p || !seen || !field || !total) {
        return;
    }

    if (seen->insert(p).second) {
        (*field)++;
        (*total)++;
    }
}

// static FaCountSummary countIrFas(const ue2::RoseBuildImpl &build) {
//     using namespace ue2;

//     FaCountSummary out;
//     set<const void *> seen_graph;
//     set<const void *> seen_dfa;
//     set<const void *> seen_haig;
//     set<const void *> seen_castle;
//     set<const void *> seen_tama;
//     set<const void *> seen_mpv;

//     auto addLeft = [&](const LeftEngInfo &left) {
//         addUniquePtr(left.graph.get(), &seen_graph, &out.graph, &out.total);
//         addUniquePtr(left.dfa.get(), &seen_dfa, &out.dfa, &out.total);
//         addUniquePtr(left.haig.get(), &seen_haig, &out.haig, &out.total);
//         addUniquePtr(left.castle.get(), &seen_castle, &out.castle, &out.total);
//         addUniquePtr(left.tamarama.get(), &seen_tama, &out.tamarama,
//                      &out.total);
//     };

//     auto addSuffix = [&](const RoseSuffixInfo &suffix) {
//         addUniquePtr(suffix.graph.get(), &seen_graph, &out.graph, &out.total);
//         addUniquePtr(suffix.rdfa.get(), &seen_dfa, &out.dfa, &out.total);
//         addUniquePtr(suffix.haig.get(), &seen_haig, &out.haig, &out.total);
//         addUniquePtr(suffix.castle.get(), &seen_castle, &out.castle,
//                      &out.total);
//         addUniquePtr(suffix.tamarama.get(), &seen_tama, &out.tamarama,
//                      &out.total);
//     };

//     for (auto v : vertices_range(build.g)) {
//         if (v == build.root || v == build.anchored_root) {
//             continue;
//         }

//         const auto &vp = build.g[v];
//         if (vp.left) {
//             addLeft(vp.left);
//         }
//         if (vp.suffix) {
//             addSuffix(vp.suffix);
//         }
//     }

//     for (const auto &outfix : build.outfixes) {
//         addUniquePtr(outfix.holder(), &seen_graph, &out.graph, &out.total);
//         addUniquePtr(outfix.rdfa(), &seen_dfa, &out.dfa, &out.total);
//         addUniquePtr(outfix.haig(), &seen_haig, &out.haig, &out.total);
//         const auto *m = outfix.mpv();
//         if (m && !m->empty()) {
//             addUniquePtr(m, &seen_mpv, &out.mpv, &out.total);
//         }
//     }

//     return out;
// }

// static FaCountSummary countBytecodeFas(const RoseEngine *rose) {
//     FaCountSummary out;
//     if (!rose) {
//         return out;
//     }

//     out.total = rose->queueCount;
//     out.mpv = rose->outfixBeginQueue ? 1 : 0;
//     out.graph = rose->outfixEndQueue >= rose->outfixBeginQueue
//                     ? (rose->outfixEndQueue - rose->outfixBeginQueue)
//                     : 0;
//     out.dfa = rose->queueCount > rose->leftfixBeginQueue
//                   ? (rose->queueCount - rose->leftfixBeginQueue)
//                   : 0;
//     return out;
// }

static const char *literalTableName(ue2::rose_literal_table t) {
    using namespace ue2;
    switch (t) {
    case ROSE_ANCHORED:
        return "anchored";
    case ROSE_FLOATING:
        return "floating";
    case ROSE_EOD_ANCHORED:
        return "eod";
    case ROSE_ANCHORED_SMALL_BLOCK:
        return "anchored_small_block";
    case ROSE_EVENT:
        return "event";
    default:
        return "unknown";
    }
}

static const char *historyName(ue2::RoseRoleHistory h) {
    using namespace ue2;
    switch (h) {
    case ROSE_ROLE_HISTORY_NONE:
        return "none";
    case ROSE_ROLE_HISTORY_ANCH:
        return "anchored";
    case ROSE_ROLE_HISTORY_LAST_BYTE:
        return "last_byte";
    case ROSE_ROLE_HISTORY_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

static string formatLiteral(const ue2::ue2_literal &lit) {
    ostringstream os;
    for (auto it = lit.begin(); it != lit.end(); ++it) {
        unsigned char c = (unsigned char)(*it).c;
        if (isprint(c) && c != '\\' && c != '"') {
            os << (char)c;
        } else {
            os << "\\x" << hex << setw(2) << setfill('0') << (unsigned)c
               << dec;
        }
    }
    return os.str();
}

static const ue2::rose_literal_id *findLiteralById(const ue2::RoseBuildImpl &b,
                                                   u32 id) {
    if (id < b.literals.size()) {
        return &b.literals.at(id);
    }
    return nullptr;
}

static string irVertexName(const ue2::RoseBuildImpl &build,
                           ue2::RoseVertex v) {
    if (v == build.root) {
        return "root";
    }
    if (v == build.anchored_root) {
        return "anchored_root";
    }

    ostringstream os;
    os << "role_" << build.g[v].index;
    if (build.g[v].eod_accept) {
        os << "(eod_accept)";
    }
    return os.str();
}

static string indentStr(int level) {
    return string(level * 2, ' ');
}

static string formatCharReachSimple(const ue2::CharReach &cr) {
    using ue2::CharReach;
    if (cr.none()) {
        return "none";
    }
    if (cr.all()) {
        return "any";
    }

    ostringstream os;
    bool first = true;
    for (size_t i = cr.find_first(); i != CharReach::npos;) {
        size_t j = i;
        while (true) {
            size_t n = cr.find_next(j);
            if (n == CharReach::npos || n != j + 1) {
                break;
            }
            j = n;
        }

        if (!first) {
            os << ",";
        }
        first = false;

        if (i == j) {
            unsigned c = (unsigned)i;
            if (isprint(c) && c != '\\' && c != '"') {
                os << (char)c;
            } else {
                os << "0x" << hex << setw(2) << setfill('0') << c << dec;
            }
        } else {
            os << "0x" << hex << setw(2) << setfill('0') << (unsigned)i
               << "-0x" << setw(2) << (unsigned)j << dec;
        }

        i = cr.find_next(j);
    }

    return os.str();
}

static void dumpNgHolderSummary(const ue2::NGHolder &h, int indent_level,
                                size_t max_nodes = 24) {
    string pad = indentStr(indent_level);
    cout << pad << "nfa vertices=" << num_vertices(h)
         << ", edges=" << num_edges(h) << "\n";

    size_t shown = 0;
    for (auto v : vertices_range(h)) {
        if (is_special(v, h)) {
            continue;
        }

        if (shown++ >= max_nodes) {
            cout << pad << "  ...\n";
            break;
        }

        cout << pad << "  v" << h[v].index
             << " chars=" << formatCharReachSimple(h[v].char_reach);

        if (!h[v].reports.empty()) {
            cout << " reports=";
            bool first = true;
            for (auto r : h[v].reports) {
                if (!first) {
                    cout << ",";
                }
                first = false;
                cout << r;
            }
        }

        cout << "\n";
    }

    cout << pad << "  transitions:\n";
    size_t edges_shown = 0;
    for (auto e : edges_range(h)) {
        if (edges_shown++ >= max_nodes * 2) {
            cout << pad << "    ...\n";
            break;
        }
        auto u = source(e, h);
        auto w = target(e, h);
        cout << pad << "    (" << h[u].index << " -> " << h[w].index << ")";
        if (!h[e].tops.empty()) {
            cout << " tops:";
            bool first = true;
            for (auto t : h[e].tops) {
                if (!first) cout << ",";
                first = false;
                cout << t;
            }
        }
        cout << "\n";
    }
}

static void dumpLeftEngineSummary(const ue2::LeftEngInfo &left,
                                  int indent_level,
                                  set<const void *> *visited_engines) {
    string pad = indentStr(indent_level);
    if (left.graph) {
        const void *key = left.graph.get();
        bool first_seen = visited_engines->insert(key).second;
        cout << pad << "left.graph @" << key;
        if (!first_seen) {
            cout << " (seen)";
        }
        cout << "\n";
        if (first_seen) {
            dumpNgHolderSummary(*left.graph, indent_level + 1);
        }
    }
    if (left.castle) {
        cout << pad << "left.castle @" << left.castle.get() << "\n";
    }
    if (left.dfa) {
        cout << pad << "left.dfa @" << left.dfa.get() << "\n";
    }
    if (left.haig) {
        cout << pad << "left.haig @" << left.haig.get() << "\n";
    }
    if (left.tamarama) {
        cout << pad << "left.tamarama @" << left.tamarama.get() << "\n";
    }
}

static void dumpSuffixEngineSummary(const ue2::RoseSuffixInfo &suffix,
                                    int indent_level,
                                    set<const void *> *visited_engines) {
    string pad = indentStr(indent_level);
    if (suffix.graph) {
        const void *key = suffix.graph.get();
        bool first_seen = visited_engines->insert(key).second;
        cout << pad << "suffix.graph @" << key;
        if (!first_seen) {
            cout << " (seen)";
        }
        cout << "\n";
        if (first_seen) {
            dumpNgHolderSummary(*suffix.graph, indent_level + 1);
        }
    }
    if (suffix.castle) {
        cout << pad << "suffix.castle @" << suffix.castle.get() << "\n";
    }
    if (suffix.rdfa) {
        cout << pad << "suffix.rdfa @" << suffix.rdfa.get() << "\n";
    }
    if (suffix.haig) {
        cout << pad << "suffix.haig @" << suffix.haig.get() << "\n";
    }
    if (suffix.tamarama) {
        cout << pad << "suffix.tamarama @" << suffix.tamarama.get() << "\n";
    }
}

static void dumpRoleDeep(const ue2::RoseBuildImpl &build, ue2::RoseVertex v,
                         int indent_level, set<size_t> *visited_roles,
                         set<const void *> *visited_engines) {
    using namespace ue2;

    const RoseGraph &g = build.g;
    const auto &vp = g[v];
    const string pad = indentStr(indent_level);

    if (!visited_roles->insert(vp.index).second) {
        cout << pad << irVertexName(build, v) << " (already visited)\n";
        return;
    }

    cout << pad << irVertexName(build, v) << "\n";

    if (!vp.literals.empty()) {
        cout << pad << "  literals:\n";
        for (u32 lit_id : vp.literals) {
            const auto *lit = findLiteralById(build, lit_id);
            if (lit) {
                cout << pad << "    - id=" << lit_id
                     << " table=" << literalTableName(lit->table)
                     << " delay=" << lit->delay
                     << " text=\"" << formatLiteral(lit->s) << "\"\n";
            } else {
                cout << pad << "    - id=" << lit_id << "\n";
            }
        }
    }

    if (vp.left) {
        cout << pad << "  requires_leftfix: lag=" << vp.left.lag
             << " leftfix_report=" << vp.left.leftfix_report << "\n";
        dumpLeftEngineSummary(vp.left, indent_level + 2, visited_engines);
    }

    if (vp.suffix) {
        cout << pad << "  triggers_suffix: top=" << vp.suffix.top << "\n";
        dumpSuffixEngineSummary(vp.suffix, indent_level + 2, visited_engines);
    }

    for (auto w : adjacent_vertices_range(v, g)) {
        if (w == build.root || w == build.anchored_root) {
            continue;
        }

        auto epair = edge(v, w, g);
        if (!epair.second) {
            continue;
        }
        const auto &ep = g[epair.first];

        cout << pad << "  -> " << irVertexName(build, w)
             << " [min=" << ep.minBound
             << ", max=" << ep.maxBound
             << ", history=" << historyName(ep.history)
             << ", top=" << ep.rose_top
             << ", cancel_prev_top=" << (unsigned)ep.rose_cancel_prev_top
             << "]\n";

        dumpRoleDeep(build, w, indent_level + 2, visited_roles,
                     visited_engines);
    }
}

static string escapeJsonString(const string& input) {
    ostringstream ss;
    for (unsigned char c : input) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\b') ss << "\\b";
        else if (c == '\f') ss << "\\f";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else if (c < 0x20) {
            ss << "\\u" << hex << setw(4) << setfill('0') << (int)c << dec;
        } else {
            ss << c;
        }
    }
    return ss.str();
}

static void printJsonReport(const string &pattern, const char *json_file) {
    using namespace ue2;

    Grey grey;
    target_t current_target = get_current_target();
    CompileContext cc(false, false, current_target, grey);

    NG ng(cc, 1, 0);
    addExpression(ng, 0, pattern.c_str(), 0, nullptr, 0);

    const auto *build = dynamic_cast<const RoseBuildImpl *>(ng.rose.get());
    if (!build) {
        throw runtime_error("Unable to access Rose compile IR");
    }

    const RoseGraph &g = build->g;

    ofstream ofs;
    streambuf *orig_buf = nullptr;
    if (json_file) {
        ofs.open(json_file);
        if (!ofs.is_open()) {
            throw runtime_error("Unable to open output JSON file");
        }
        orig_buf = cout.rdbuf(ofs.rdbuf());
    }

    cout << "{\n";
    cout << "  \"regex\": \"" << escapeJsonString(pattern) << "\",\n";

    // 1. Literals (Roles)
    cout << "  \"roles\": [\n";
    bool first_lit = true;
    for (auto v : vertices_range(g)) {
        string role_id = irVertexName(*build, v);
        string lit_str = "";
        const auto &vp = g[v];
        
        bool first_s = true;
        for (u32 lit_id : vp.literals) {
            const auto *lit = findLiteralById(*build, lit_id);
            if (lit) {
                if (!first_s) lit_str += "|";
                lit_str += formatLiteral(lit->s);
                first_s = false;
            }
        }
        if (lit_str.empty()) lit_str = "<none>";

        if (!first_lit) cout << ",\n";
        cout << "    { \"id\": \"" << escapeJsonString(role_id) 
             << "\", \"literal\": \"" << escapeJsonString(lit_str) << "\" }";
        first_lit = false;
    }
    cout << "\n  ],\n";

    // Collect Unique FAs
    map<const void*, string> fa_ids;
    size_t fa_counter = 0;
    auto get_fa_id = [&](const void *ptr) {
        if (fa_ids.find(ptr) == fa_ids.end()) {
            fa_ids[ptr] = "fa_" + to_string(++fa_counter);
        }
        return fa_ids[ptr];
    };

    vector<const NGHolder*> all_fas;
    for (auto v : vertices_range(g)) {
        if (g[v].left && g[v].left.graph) {
            if (fa_ids.find(g[v].left.graph.get()) == fa_ids.end()) {
                all_fas.push_back(g[v].left.graph.get());
                get_fa_id(g[v].left.graph.get());
            }
        }
        if (g[v].suffix && g[v].suffix.graph) {
            if (fa_ids.find(g[v].suffix.graph.get()) == fa_ids.end()) {
                all_fas.push_back(g[v].suffix.graph.get());
                get_fa_id(g[v].suffix.graph.get());
            }
        }
    }

    // 2. FAs
    cout << "  \"FAs\": [\n";
    bool first_fa = true;
    for (const NGHolder* h : all_fas) {
        if (!first_fa) cout << ",\n";
        first_fa = false;

        cout << "    {\n";
        cout << "      \"id\": \"" << fa_ids[h] << "\",\n";
        
        // Nodes
        cout << "      \"nodes\": [\n";
        bool first_node = true;
        for (auto v : vertices_range(*h)) {
            if (!first_node) cout << ",\n";
            first_node = false;

            string chars;
            if (is_special(v, *h)) {
                switch ((*h)[v].index) {
                    case ue2::NODE_START: chars = "START"; break;
                    case ue2::NODE_START_DOTSTAR: chars = "START_DOTSTAR"; break;
                    case ue2::NODE_ACCEPT: chars = "ACCEPT"; break;
                    case ue2::NODE_ACCEPT_EOD: chars = "ACCEPT_EOD"; break;
                    default: chars = "SPECIAL"; break;
                }
            } else {
                chars = formatCharReachSimple((*h)[v].char_reach);
            }

            cout << "        { \"id\": \"" << (*h)[v].index 
                 << "\", \"chars\": \"" << escapeJsonString(chars) << "\" }";
        }
        cout << "\n      ],\n";

        // Transitions
        cout << "      \"transitions\": [\n";
        bool first_edge = true;
        for (auto e : edges_range(*h)) {
            auto u = source(e, *h);
            auto w = target(e, *h);
            
            string lbl = "";
            if (!(*h)[e].tops.empty()) {
                lbl += "tops:";
                bool f = true;
                for (auto t : (*h)[e].tops) {
                    if (!f) lbl += ",";
                    lbl += to_string(t);
                    f = false;
                }
            }
            
            if (!first_edge) cout << ",\n";
            first_edge = false;
            cout << "        [\"" << (*h)[u].index << "\", \"" << (*h)[w].index 
                 << "\", \"" << escapeJsonString(lbl) << "\"]";
        }
        cout << "\n      ]\n";
        cout << "    }";
    }
    cout << "\n  ],\n";

    // 3. Triggers
    cout << "  \"triggers\": [\n";
    bool first_trigger = true;

    auto print_trigger = [&](const string &src, const string &tgt, const string &lbl) {
        if (!first_trigger) cout << ",\n";
        first_trigger = false;
        cout << "    [\"" << escapeJsonString(src) << "\", \"" 
             << escapeJsonString(tgt) << "\", \"" << escapeJsonString(lbl) << "\"]";
    };

    for (auto e : edges_range(g)) {
        auto u = source(e, g);
        auto w = target(e, g);
        const auto &ep = g[e];

        string src_id = irVertexName(*build, u);
        string tgt_id = irVertexName(*build, w);
        
        string lbl = "min:" + to_string(ep.minBound) + " max:";
        if (ep.maxBound == ROSE_BOUND_INF) lbl += "inf";
        else lbl += to_string(ep.maxBound);
        
        if (g[w].left && g[w].left.graph) {
            // u triggers a leftfix FA
            string fa_id = fa_ids[g[w].left.graph.get()];
            print_trigger(src_id, fa_id, lbl + " top:" + to_string(ep.rose_top));
            print_trigger(fa_id, tgt_id, "leftfix_accept");
        } else {
            // u triggers w directly (simple distance bounds)
            print_trigger(src_id, tgt_id, lbl);
        }
    }
    
    // Handle suffix FAs triggered by nodes
    for (auto v : vertices_range(g)) {
        if (g[v].suffix && g[v].suffix.graph) {
            string src_id = irVertexName(*build, v);
            string fa_id = fa_ids[g[v].suffix.graph.get()];
            print_trigger(src_id, fa_id, "suffix top:" + to_string(g[v].suffix.top));
        }
    }

    cout << "\n  ]\n";
    cout << "}\n";

    if (orig_buf) {
        cout.rdbuf(orig_buf);
    }
}

} // namespace




int __cdecl main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        cerr << "Usage: " << argv[0] << " <regex> [output_json]" << endl;
        return 1;
    }

    const string pattern = argv[1];
    const char *json_file = (argc == 3) ? argv[2] : nullptr;

    try {
        printJsonReport(pattern, json_file);
        return 0;
    } catch (const exception &e) {
        cerr << "IR compile error: " << e.what() << endl;
        return 1;
    }
}