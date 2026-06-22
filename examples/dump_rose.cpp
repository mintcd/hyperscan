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
#include "util/dump_charclass.h"
#include "util/container.h"
#include "nfagraph/ng_reports.h"

// Hyperscan internal headers for AST extraction
#include "parser/Component.h"
#include "parser/ComponentSequence.h"
#include "parser/ComponentAlternation.h"
#include "parser/ComponentRepeat.h"
#include "parser/dump.h"
#include "parser/AsciiComponentClass.h"

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
#include <sys/stat.h>

#include "my_utils.h"

#ifdef _WIN32
#undef min
#undef max
#endif

using namespace std;



static const ue2::rose_literal_id *findLiteralById(const ue2::RoseBuildImpl &b,
                                                   u32 id) {
    if (id < b.literals.size()) {
        return &b.literals.at(id);
    }
    return nullptr;
}

static string getVertexNameId(const ue2::RoseBuildImpl &build,
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


static void dumpRoles(ostream &os, const ue2::RoseBuildImpl &build, const ue2::RoseGraph &g) {
    using namespace ue2;
    os << "  \"roles\": [\n";
    bool first_lit = true;
    for (RoseVertex v : vertices_range(g)) {
        string role_id = getVertexNameId(build, v);
        string lit_str = "";
        const RoseVertexProps &vp = g[v];
        
        bool first_s = true;
        for (u32 lit_id : vp.literals) {
            const auto *lit = findLiteralById(build, lit_id);
            if (lit) {
                if (!first_s) lit_str += "|";
                lit_str += formatLiteral(lit->s);
                first_s = false;
            }
        }
        if (lit_str.empty()) lit_str = "<none>";

        if (!first_lit) os << ",\n";
        os << "    { \"id\": \"" << escapeJsonString(role_id)
           << "\", \"literal\": \"" << escapeJsonString(lit_str) << "\"";

        if (!vp.reports.empty()) {
            os << ", \"reports\": [" << as_string_list(vp.reports) << "]";
        }

        if (vp.left && vp.left.graph) {
            left_id l(vp.left);
            os << ", \"left\": [" << as_string_list(all_reports(l)) << "]";
        }

        if (vp.suffix && vp.suffix.graph) {
            suffix_id s(vp.suffix);
            os << ", \"suffix_top\": " << vp.suffix.top;
            os << ", \"suffix\": [" << as_string_list(all_reports(s)) << "]";
        }

        os << " }";
        first_lit = false;
    }
    os << "\n  ],\n";
}

static void dumpFAs(ostream &os, const vector<const ue2::NGHolder*> &all_fas, map<const void*, string> &fa_ids) {
    using namespace ue2;
    os << "  \"FAs\": [\n";
    bool first_fa = true;
    for (const NGHolder* h : all_fas) {
        if (!first_fa) os << ",\n";
        first_fa = false;

        os << "    {\n";
        os << "      \"id\": \"" << fa_ids[h] << "\",\n";
        os << "      \"reports\": [" << as_string_list(all_reports(*h)) << "],\n";
        
        // Nodes
        os << "      \"nodes\": [\n";
        bool first_node = true;
        for (auto v : vertices_range(*h)) {
            if (!first_node) os << ",\n";
            first_node = false;

            string chars;
            if (is_special(v, *h)) {
                switch ((*h)[v].index) {
                    case NODE_START: chars = "START"; break;
                    case NODE_START_DOTSTAR: chars = "START_DOTSTAR"; break;
                    case NODE_ACCEPT: chars = "ACCEPT"; break;
                    case NODE_ACCEPT_EOD: chars = "ACCEPT_EOD"; break;
                    default: chars = "SPECIAL"; break;
                }
            } else {
                chars = formatCharReachSimple((*h)[v].char_reach);
            }

            os << "        { \"id\": \"" << (*h)[v].index 
               << "\", \"chars\": \"" << escapeJsonString(chars) << "\" }";
        }
        os << "\n      ],\n";

        // Transitions
        os << "      \"transitions\": [\n";
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
            
            if (!first_edge) os << ",\n";
            first_edge = false;
            os << "        [\"" << (*h)[u].index << "\", \"" << (*h)[w].index 
               << "\", \"" << escapeJsonString(lbl) << "\"]";
        }
        os << "\n      ]\n";
        os << "    }";
    }
    os << "\n  ],\n";
}

static void dumpTriggers(ostream &os, const ue2::RoseBuildImpl &build, const ue2::RoseGraph &g, map<const void*, string> &fa_ids) {
    using namespace ue2;
    os << "  \"triggers\": [\n";
    bool first_trigger = true;

    auto print_trigger = [&](const string &src, const string &tgt, const string &lbl) {
        if (!first_trigger) os << ",\n";
        first_trigger = false;
        os << "    [\"" << escapeJsonString(src) << "\", \"" 
           << escapeJsonString(tgt) << "\", \"" << escapeJsonString(lbl) << "\"]";
    };

    for (auto e : edges_range(g)) {
        auto u = source(e, g);
        auto w = target(e, g);
        const auto &ep = g[e];

        string src_id = getVertexNameId(build, u);
        string tgt_id = getVertexNameId(build, w);

        string lbl = "min:" + to_string(ep.minBound) + " max:";
        if (ep.maxBound == ROSE_BOUND_INF) lbl += "inf";
        else lbl += to_string(ep.maxBound);
        
        // Include the label bounds with <linear>
        print_trigger(src_id, tgt_id, "<linear> " + lbl);

        if (g[w].left && g[w].left.graph) {
            string fa_id = fa_ids[g[w].left.graph.get()];
            print_trigger(src_id, fa_id, "<turnon>");
            print_trigger(tgt_id, fa_id, "<verify>");
        }
    }
    
    // Handle suffix FAs triggered by nodes
    for (RoseVertex v : vertices_range(g)) {
        if (g[v].suffix && g[v].suffix.graph) {
            string src_id = getVertexNameId(build, v);
            string fa_id = fa_ids[g[v].suffix.graph.get()];
            print_trigger(src_id, fa_id, "suffix top:" + to_string(g[v].suffix.top));
        }
    }

    os << "\n  ]\n";
}

static void printJsonReport(const string &pattern, std::ostream &os) {
    using namespace ue2;

    Grey grey; 
    target_t current_target = get_current_target();
    CompileContext cc(false, false, current_target, grey);

    NG ng(cc, 1, 0);
    addExpression(ng, 0, pattern.c_str(), 0, nullptr, 0);

    const auto *build = dynamic_cast<const RoseBuildImpl *>(ng.rose.get());
    const RoseGraph &g = build->g;

    os << "{\n";
    os << "  \"regex\": \"" << escapeJsonString(pattern) << "\",\n";

    dumpRoles(os, *build, g);

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

    dumpFAs(os, all_fas, fa_ids);
    dumpTriggers(os, *build, g, fa_ids);

    os << "}\n";
}

int __cdecl main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        cerr << "Usage: " << argv[0] << " <regex> [output_json]" << endl;
        return 1;
    }

    const string pattern = argv[1];
    const char *json_file = (argc == 3) ? argv[2] : nullptr;

    try {
        if (json_file) {
            string out_path(json_file);
            size_t pos = out_path.find_last_of("/\\");
            if (pos != string::npos) {
                string parent = out_path.substr(0, pos);
                create_directories(parent);
            }
            ofstream ofs(json_file);
            if (!ofs.is_open()) {
                throw runtime_error("Unable to open output JSON file");
            }
            printJsonReport(pattern, ofs);
        } else {
            printJsonReport(pattern, cout);
        }
        return 0;
    } catch (const exception &e) {
        cerr << "IR compile error: " << e.what() << endl;
        return 1;
    }
}