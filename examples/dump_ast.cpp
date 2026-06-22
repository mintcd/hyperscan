#include "config.h"

#include "compiler/compiler.h"
#include "parser/Component.h"
#include "parser/ComponentSequence.h"
#include "parser/ComponentAlternation.h"
#include "parser/ComponentRepeat.h"
#include "parser/dump.h"
#include "parser/AsciiComponentClass.h"

#include "my_utils.h"

#include <cctype>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

static string astToJson(const ue2::Component *comp) {
    if (!comp) return "null";

    using namespace ue2;
    ostringstream ss;

    class JsonVisitor : public ue2::DefaultConstComponentVisitor {
        using ue2::DefaultConstComponentVisitor::pre;
        using ue2::DefaultConstComponentVisitor::during;
        using ue2::DefaultConstComponentVisitor::post;
        ostringstream &ss;
    public:
        JsonVisitor(ostringstream &s) : ss(s) {}

        void pre(const ue2::ComponentSequence &c) override {
            ss << "{ \"type\": \"Sequence\"";
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
            ss << "{ \"type\": \"Alternation\", \"children\": [";
        }
        void during(const ue2::ComponentAlternation &) override { ss << ", "; }
        void post(const ue2::ComponentAlternation &) override { ss << "] }"; }

        void pre(const ue2::ComponentRepeat &c) override {
            ss << "{ \"type\": \"Repeat\", \"children\": [";
        }
        void post(const ue2::ComponentRepeat &) override { ss << "] }"; }

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

        void pre(const ue2::AsciiComponentClass &c) override {
            const ue2::CharReach &cr = c.getCharReach();

            if (cr.none()) {
                ss << "{ \"type\": \"AsciiComponentClass\", \"chars\": []";
                return;
            }

            if (cr.count() == 1) {
                unsigned ch = (unsigned)cr.find_first();
                std::ostringstream tmp;
                if (isprint(ch) && ch != '\\' && ch != '"') tmp << (char)ch;
                else tmp << "\\x" << std::hex << std::setw(2) << std::setfill('0') << ch << std::dec;
                ss << "{ \"type\": \"Literal\", \"value\": \"" << escapeJsonString(tmp.str()) << "\"";
                return;
            }

            size_t first = cr.find_first();
            size_t last = first;
            size_t it = first;
            bool contiguous = true;
            while ((it = cr.find_next(it)) != ue2::CharReach::npos) {
                if (it != last + 1) { contiguous = false; break; }
                last = it;
            }

            if (contiguous) {
                std::ostringstream a, b;
                a << "0x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned)first << std::dec;
                b << "0x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned)last << std::dec;
                ss << "{ \"type\": \"Range\", \"value\": [\"" << a.str() << "\", \"" << b.str() << "\"]";
                return;
            }

            ss << "{ \"type\": \"AsciiComponentClass\", \"chars\": [";
            bool first_out = true;
            for (size_t j = cr.find_first(); j != ue2::CharReach::npos; j = cr.find_next(j)) {
                if (!first_out) ss << ", ";
                first_out = false;
                std::ostringstream tmp;
                if (isprint((int)j) && j != '\\' && j != '"') tmp << (char)j;
                else tmp << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned)j << std::dec;
                ss << "\"" << escapeJsonString(tmp.str()) << "\"";
            }
            ss << "]";
        }
        void post(const ue2::AsciiComponentClass &) override { ss << " }"; }

        void pre(const ue2::UTF8ComponentClass &c) override { std::ostringstream doss; ue2::dumpTree(doss, reinterpret_cast<const ue2::Component *>(&c)); ss << "{ \"type\": \"UTF8ComponentClass\", \"dump\": \"" << escapeJsonString(doss.str()) << "\""; }
        void post(const ue2::UTF8ComponentClass &) override { ss << " }"; }
    };

    JsonVisitor v(ss);
    comp->accept(v);
    return ss.str();
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <regex>" << endl;
        return 1;
    }

    const string pattern = argv[1];

    try {
        ue2::ParsedExpression pe(0, pattern.c_str(), 0, 0, nullptr);
        string ast_json = astToJson(pe.component.get());
        
        cout << "{\n";
        cout << "  \"regex\": \"" << escapeJsonString(pattern) << "\",\n";
        cout << "  \"tree\": " << ast_json << "\n";
        cout << "}\n";
        return 0;
    } catch (const exception &e) {
        cerr << "Parse error: " << e.what() << endl;
        return 1;
    }
}
