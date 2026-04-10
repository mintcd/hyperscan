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

// Hyperscan internal headers for AST extraction
#include "parser/Component.h"
#include "parser/ComponentSequence.h"
#include "parser/ComponentAlternation.h"
#include "parser/ComponentRepeat.h"
#include "parser/dump.h"

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

// Convert a Hyperscan AST Component to JSON using the public visitor API.
static string astToJson(const ue2::Component *comp) {
    if (!comp) return "null";

    using namespace ue2;
    ostringstream ss;

    // Visitor that emits JSON for each component using the ConstComponentVisitor
    // callbacks. This avoids accessing protected/private members directly.
    class JsonVisitor : public ue2::DefaultConstComponentVisitor {
        using ue2::DefaultConstComponentVisitor::pre;
        using ue2::DefaultConstComponentVisitor::during;
        using ue2::DefaultConstComponentVisitor::post;
        ostringstream &ss;
    public:
        JsonVisitor(ostringstream &s) : ss(s) {}

        void pre(const ue2::ComponentSequence &c) override {
            std::ostringstream doss;
            ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c));
            ss << "{ \"type\": \"Sequence\", \"dump\": \"" << escapeJsonString(doss.str()) << "\"";
            unsigned idx = c.getCaptureIndex();
            if (idx != ue2::ComponentSequence::NOT_CAPTURED) {
                ss << ", \"capture_index\": " << idx;
            } else {
                ss << ", \"capture_index\": null";
            }
            const std::string &nm = c.getCaptureName();
            if (!nm.empty()) ss << ", \"capture_name\": \"" << escapeJsonString(nm) << "\"";
            ss << ", \"children\": [";
        }
        void during(const ue2::ComponentSequence &) override { ss << ", "; }
        void post(const ue2::ComponentSequence &) override { ss << "] }"; }

        void pre(const ue2::ComponentAlternation &c) override {
            std::ostringstream doss;
            ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c));
            ss << "{ \"type\": \"Alternation\", \"dump\": \"" << escapeJsonString(doss.str()) << "\", \"children\": [";
        }
        void during(const ue2::ComponentAlternation &) override { ss << ", "; }
        void post(const ue2::ComponentAlternation &) override { ss << "] }"; }

        void pre(const ue2::ComponentRepeat &c) override {
            auto b = c.getBounds();
            std::ostringstream doss;
            ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c));
            ss << "{ \"type\": \"Repeat\", \"dump\": \"" << escapeJsonString(doss.str()) << "\", \"min\": " << b.first << ", \"max\": ";
            if (b.second == ComponentRepeat::NoLimit) ss << "\"inf\"";
            else ss << b.second;
            ss << ", \"child\": ";
        }
        void post(const ue2::ComponentRepeat &) override { ss << " }"; }

        // Leaf/default components: emit a simple type object.
        void pre(const ue2::ComponentByte &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"Byte\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::ComponentByte &) override { ss << " }"; }

        

        void pre(const ue2::ComponentEmpty &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"Empty\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::ComponentEmpty &) override { ss << " }"; }

        void pre(const ue2::ComponentBoundary &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"Boundary\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::ComponentBoundary &) override { ss << " }"; }

        void pre(const ue2::ComponentAssertion &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"Assertion\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::ComponentAssertion &) override { ss << " }"; }

        void pre(const ue2::ComponentBackReference &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"BackReference\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::ComponentBackReference &) override { ss << " }"; }

        void pre(const ue2::ComponentCondReference &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"CondReference\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::ComponentCondReference &) override { ss << " }"; }

        void pre(const ue2::ComponentAtomicGroup &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"AtomicGroup\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::ComponentAtomicGroup &) override { ss << " }"; }

        void pre(const ue2::ComponentEUS &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"EUS\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::ComponentEUS &) override { ss << " }"; }

        void pre(const ue2::ComponentWordBoundary &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"WordBoundary\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::ComponentWordBoundary &) override { ss << " }"; }

        void pre(const ue2::AsciiComponentClass &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"AsciiComponentClass\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::AsciiComponentClass &) override { ss << " }"; }

        void pre(const ue2::UTF8ComponentClass &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"UTF8ComponentClass\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::UTF8ComponentClass &) override { ss << " }"; }
    };

    JsonVisitor v(ss);
    comp->accept(v);
    return ss.str();
}

static void printJsonReport(const string &pattern, const char *json_file) {
    using namespace ue2;

    Grey grey;
    target_t current_target = get_current_target();
    CompileContext cc(false, false, current_target, grey);

    // Extract AST
    ParsedExpression pe(0, pattern.c_str(), 0, 0, nullptr);
    string init_ast_json = astToJson(pe.component.get());

    // pe.component->optimise(true /* root is connected to sds */);
    // string last_ast_json = astToJson(pe.component.get());

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
    cout << "  \"tree\": " << init_ast_json << ",\n";
    // cout << "  \"last_ast\": " << last_ast_json << ",\n";

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

        printf("Edge (%s, %s)", src_id.c_str(), tgt_id.c_str());
        
        string lbl = "min:" + to_string(ep.minBound) + " max:";
        if (ep.maxBound == ROSE_BOUND_INF) lbl += "inf";
        else lbl += to_string(ep.maxBound);
        
        // [MY NOTE] u triggers w with lbl
        print_trigger(src_id, tgt_id, "<linear>");

        if (g[w].left && g[w].left.graph) {
            // [MY NOTE] u and w trigger left engine of w with lbl
            string fa_id = fa_ids[g[w].left.graph.get()];
            print_trigger(src_id, fa_id, "<turnon>");
            print_trigger(tgt_id, fa_id, "<verify>");
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