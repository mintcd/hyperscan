// Simple example: compile a regex into an NFA and print vertices/edges
#include <iostream>
#include <exception>
#include <cstdlib>
#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>

#include "compiler/compiler.h"
#include "util/compile_context.h"
#include "util/target_info.h"
#include "grey.h"
#include "util/report_manager.h"
#include "nfagraph/ng.h"
#include "nfagraph/ng_holder.h"
#include "util/graph_range.h"

#include "nfagraph/ng_dominators.h"
#include "nfagraph/ng_region.h"
#include "nfagraph/ng_netflow.h"
#include "nfagraph/ng_literal_analysis.h"
#include "nfagraph/ng_util.h"

#include <map>
#include <set>
#include <queue>
#include <unordered_set>
#include <functional>
#include "nfagraph/ng_split.h"
#include "util/ue2string.h"

using namespace ue2;

static std::string litToString(const ue2_literal &lit) {
    std::string out;
    out.reserve(lit.length());
    for (auto it = lit.begin(); it != lit.end(); ++it) {
        auto e = *it; // ue2_literal::elem
        out.push_back(e.c);
    }
    return out;
}

// Find candidate pivots using a simplified dominant-path extraction.
static std::set<NFAVertex> findCandidatePivotsSimple(const NGHolder &g) {
    auto dominators = findDominators(g);
    std::set<NFAVertex> accepts;
    for (auto v : inv_adjacent_vertices_range(g.accept, g)) {
        if (!is_special(v, g)) accepts.insert(v);
    }
    for (auto v : inv_adjacent_vertices_range(g.acceptEod, g)) {
        if (!is_special(v, g)) accepts.insert(v);
    }

    std::vector<NFAVertex> dom_trace;
    if (!accepts.empty()) {
        auto ait = accepts.begin();
        NFAVertex curr = *ait;
        while (curr && !is_special(curr, g)) {
            dom_trace.push_back(curr);
            auto it = dominators.find(curr);
            if (it == dominators.end()) break;
            curr = it->second;
        }
        std::reverse(dom_trace.begin(), dom_trace.end());
        for (++ait; ait != accepts.end(); ++ait) {
            curr = *ait;
            std::vector<NFAVertex> dom_trace2;
            while (curr && !is_special(curr, g)) {
                dom_trace2.push_back(curr);
                auto it = dominators.find(curr);
                if (it == dominators.end()) break;
                curr = it->second;
            }
            std::reverse(dom_trace2.begin(), dom_trace2.end());
            auto dti = dom_trace.begin(), dtie = dom_trace.end();
            auto dtj = dom_trace2.begin(), dtje = dom_trace2.end();
            while (dti != dtie && dtj != dtje && *dti == *dtj) {
                ++dti; ++dtj;
            }
            dom_trace.erase(dti, dtie);
        }
    }

    std::set<NFAVertex> cand_raw(dom_trace.begin(), dom_trace.end());
    std::set<NFAVertex> cand;
    for (auto u : cand_raw) {
        const CharReach &u_cr = g[u].char_reach;
        if (u_cr.count() > 40) continue;
        if (u_cr.count() > 2) { cand.insert(u); continue; }
        NFAVertex v = getSoleDestVertex(g, u);
        if (v && in_degree(v, g) == 1 && out_degree(u, g) == 1) {
            const CharReach &v_cr = g[v].char_reach;
            if (v_cr.count() == 1 || v_cr.isCaselessChar()) {
                continue;
            }
        }
        cand.insert(u);
    }

    return cand;
}

// Recursive decomposition printer: pick best pivot, split and recurse into RHS.
static void decomposeGraphRec(const NGHolder &g, int depth = 0, int maxDepth = 3) {
    std::string indent(depth * 2, ' ');
    std::cout << indent << "Graph: states=" << num_vertices(g)
              << " edges=" << num_edges(g) << "\n";

    if (depth >= maxDepth) {
        std::cout << indent << "  (max depth reached)\n";
        return;
    }

    auto cand = findCandidatePivotsSimple(g);
    struct Candidate { NFAVertex v; u64a score; std::set<ue2_literal> lits; };
    std::vector<Candidate> cand_list;

    for (auto v : cand) {
        auto lits = getLiteralSet(g, v, true);
        if (lits.empty()) continue;
        auto lits_copy = lits;
        u64a sc = sanitizeAndCompressAndScore(lits_copy);
        cand_list.push_back({v, sc, std::move(lits_copy)});
    }

    if (cand_list.empty()) {
        std::cout << indent << "  No candidate pivots with literals\n";
        return;
    }

    std::sort(cand_list.begin(), cand_list.end(),
              [](const Candidate &a, const Candidate &b) { return a.score < b.score; });

    const Candidate &best = cand_list.front();
    std::cout << indent << "  Best pivot v" << g[best.v].index
              << " score=" << best.score << " lits=[";
    bool first = true;
    for (const auto &lit : best.lits) {
        if (!first) std::cout << ", ";
        first = false;
        std::cout << litToString(lit);
    }
    std::cout << "]\n";

    NGHolder lhs; NGHolder rhs;
    std::unordered_map<NFAVertex, NFAVertex> lhs_map, rhs_map;
    splitGraph(g, best.v, &lhs, &lhs_map, &rhs, &rhs_map);

    std::cout << indent << "  RHS: states=" << num_vertices(rhs)
              << " edges=" << num_edges(rhs) << "\n";

    // Small summary of literal sets found in RHS
    for (auto v : vertices_range(rhs)) {
        auto s = getLiteralSet(rhs, v, true);
        if (!s.empty()) {
            std::cout << indent << "    vertex " << rhs[v].index << " lits=[";
            bool f2 = true;
            for (const auto &lit : s) {
                if (!f2) std::cout << ", "; f2 = false;
                std::cout << litToString(lit);
            }
            std::cout << "]\n";
        }
    }

    if (num_vertices(rhs) > NGHolder::N_SPECIAL_VERTICES) {
        decomposeGraphRec(rhs, depth + 1, maxDepth);
    }
}

int __cdecl main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: dump_nfa <regex> [flags]\n";
        return 1;
    }

    const char *pattern = argv[1];
    unsigned flags = 0;
    if (argc >= 3) {
        flags = static_cast<unsigned>(std::atoi(argv[2]));
    }

    try {
        ParsedExpression pe(0, pattern, flags, 0, nullptr);

        Grey grey;
        ReportManager rm(grey);
        target_t cur_target = get_current_target();
        CompileContext cc(false, false, cur_target, grey);

        auto built = buildGraph(rm, cc, pe);
        if (!built.g) {
            std::cerr << "Failed to build NFA.\n";
            return 2;
        }

        NGHolder &h = *built.g;

        std::cout << "Vertices: " << num_vertices(h) << "\n";

        auto vertex_name = [&](auto v) -> std::string {
            if (is_special(v, h)) {
                switch (h[v].index) {
                case NODE_START: return "NODE_START";
                case NODE_START_DOTSTAR: return "NODE_START_DOTSTAR";
                case NODE_ACCEPT: return "NODE_ACCEPT";
                case NODE_ACCEPT_EOD: return "NODE_ACCEPT_EOD";
                default: return "SPECIAL";
                }
            }
            return std::string("v") + std::to_string(h[v].index);
        };

        auto formatCharReach = [&](const ue2::CharReach &cr) {
            using ue2::CharReach;
            if (cr.none()) return std::string();
            // If all bytes are set, report as "any" (dot).
            if (cr.all()) return std::string("any");
            std::ostringstream oss;
            bool first = true;
            for (size_t i = cr.find_first(); i != CharReach::npos; ) {
                size_t j = i;
                while (true) {
                    size_t n = cr.find_next(j);
                    if (n == CharReach::npos || n != j + 1) break;
                    j = n;
                }
                if (!first) oss << ",";
                first = false;
                if (i == j) {
                    unsigned c = static_cast<unsigned>(i);
                    if (std::isprint((int)c)) {
                        if (c == '\'' || c == '\\') oss << "'\\" << static_cast<char>(c) << "'";
                        else oss << "'" << static_cast<char>(c) << "'";
                    } else {
                        oss << "0x" << std::hex << std::setw(2) << std::setfill('0') << i << std::dec;
                    }
                } else {
                    oss << "0x" << std::hex << std::setw(2) << std::setfill('0') << i
                        << "-0x" << std::setw(2) << j << std::dec;
                }
                i = cr.find_next(j);
            }
            return oss.str();
        };

        for (auto v : vertices_range(h)) {
            std::cout << h[v].index << " " << vertex_name(v);
            if (!h[v].char_reach.none()) {
                std::cout << " chars=" << formatCharReach(h[v].char_reach) << "";
            } else {
                std::cout << " chars=none";
            }
            // if (!h[v].reports.empty()) {
            //     std::cout << " reports:";
            //     for (auto r : h[v].reports) std::cout << r << ",";
            // }
            std::cout << "\n";
        }

        std::cout << "\n";
        std::cout << "Edges: " << num_edges(h) << "\n";
        

        for (auto e : edges_range(h)) {
            auto u = source(e, h);
            auto w = target(e, h);
            std::cout << h[e].index << " (" << h[u].index << "," << h[w].index << ")";
            if (!h[w].char_reach.none()) {
                std::cout << " on=" << formatCharReach(h[w].char_reach);
            } else {
                std::cout << " on=none";
            }

            if (!h[e].tops.empty()) {
                std::cout << " tops:";
                for (auto t : h[e].tops) std::cout << t << ",";
            }
            std::cout << "\n";
        }
        
        // --- Simple analyses: dominators, candidate pivots, regions, min-cut ---
        std::cout << "\n--- Analysis ---\n";

        // Dominator analysis
        auto doms = findDominators(h);
        std::cout << "Dominator (node -> immediate dominator)\n";
        std::unordered_map<NFAVertex, std::vector<NFAVertex>> dom_children;
        for (const auto &p : doms) {
            dom_children[p.second].push_back(p.first);
        }

        std::unordered_map<NFAVertex, unsigned> dom_depth;
        std::function<unsigned(NFAVertex)> compute_depth =
            [&](NFAVertex v) -> unsigned {
                auto it = dom_depth.find(v);
                if (it != dom_depth.end()) return it->second;
                auto it2 = doms.find(v);
                if (it2 == doms.end()) {
                    dom_depth[v] = 0;
                    return 0;
                }
                unsigned d = compute_depth(it2->second) + 1;
                dom_depth[v] = d;
                return d;
            };

        for (auto v : vertices_range(h)) {
            compute_depth(v);
        }

        std::unordered_map<NFAVertex, unsigned> dom_subtree;
        std::function<unsigned(NFAVertex)> compute_subtree =
            [&](NFAVertex v) -> unsigned {
                unsigned sz = 1;
                auto it = dom_children.find(v);
                if (it != dom_children.end()) {
                    for (auto c : it->second) sz += compute_subtree(c);
                }
                dom_subtree[v] = sz;
                return sz;
            };

        // compute subtree sizes from roots
        for (auto v : vertices_range(h)) {
            if (doms.find(v) == doms.end()) {
                compute_subtree(v);
            }
        }

        for (auto v : vertices_range(h)) {
            std::cout << h[v].index << " " << vertex_name(v) << " idom=";
            auto it = doms.find(v);
            if (it == doms.end()) {
                std::cout << "none";
            } else {
                std::cout << h[it->second].index;
            }
            std::cout << " depth=" << dom_depth[v]
                      << " subtree=" << dom_subtree[v] << "\n";
        }

        // Candidate pivots (dominant-path)
        std::set<NFAVertex> accepts;
        for (auto v : inv_adjacent_vertices_range(h.accept, h)) {
            if (!is_special(v, h)) accepts.insert(v);
        }
        for (auto v : inv_adjacent_vertices_range(h.acceptEod, h)) {
            if (!is_special(v, h)) accepts.insert(v);
        }

        std::vector<NFAVertex> dom_trace;
        if (!accepts.empty()) {
            auto ait = accepts.begin();
            NFAVertex curr = *ait;
            while (curr && !is_special(curr, h)) {
                dom_trace.push_back(curr);
                auto it = doms.find(curr);
                if (it == doms.end()) break;
                curr = it->second;
            }
            std::reverse(dom_trace.begin(), dom_trace.end());
            for (++ait; ait != accepts.end(); ++ait) {
                curr = *ait;
                std::vector<NFAVertex> dom_trace2;
                while (curr && !is_special(curr, h)) {
                    dom_trace2.push_back(curr);
                    auto it = doms.find(curr);
                    if (it == doms.end()) break;
                    curr = it->second;
                }
                std::reverse(dom_trace2.begin(), dom_trace2.end());
                auto dti = dom_trace.begin(), dtie = dom_trace.end();
                auto dtj = dom_trace2.begin(), dtje = dom_trace2.end();
                while (dti != dtie && dtj != dtje && *dti == *dtj) {
                    ++dti; ++dtj;
                }
                dom_trace.erase(dti, dtie);
            }
        }

        std::set<NFAVertex> cand_raw(dom_trace.begin(), dom_trace.end());
        std::set<NFAVertex> cand;
        // filter similar to ng_violet::filterCandPivots
        for (auto u : cand_raw) {
            const CharReach &u_cr = h[u].char_reach;
            if (u_cr.count() > 40) continue;
            if (u_cr.count() > 2) { cand.insert(u); continue; }
            NFAVertex v = getSoleDestVertex(h, u);
            if (v && in_degree(v, h) == 1 && out_degree(u, h) == 1) {
                const CharReach &v_cr = h[v].char_reach;
                if (v_cr.count() == 1 || v_cr.isCaselessChar()) {
                    continue;
                }
            }
            cand.insert(u);
        }

        auto count_reachable_simple = [&](NFAVertex s, const NGHolder &g,
                                          const std::unordered_set<size_t> &skip_edges)
            -> size_t {
            std::queue<NFAVertex> q;
            std::unordered_set<NFAVertex> seen;
            q.push(s);
            seen.insert(s);
            while (!q.empty()) {
                NFAVertex u = q.front(); q.pop();
                for (const auto &e : out_edges_range(u, g)) {
                    if (skip_edges.count(g[e].index)) continue;
                    NFAVertex w = target(e, g);
                    if (seen.insert(w).second) q.push(w);
                }
            }
            return seen.size();
        };

        std::cout << "\nCandidate pivots:\n";
        for (auto v : cand) {
            size_t fwd = count_reachable_simple(v, h, {});
            // backward reach: do reverse traversal
            std::queue<NFAVertex> q;
            std::unordered_set<NFAVertex> seen;
            q.push(v); seen.insert(v);
            while (!q.empty()) {
                NFAVertex u = q.front(); q.pop();
                for (const auto &e : in_edges_range(u, h)) {
                    NFAVertex p = source(e, h);
                    if (seen.insert(p).second) q.push(p);
                }
            }
            size_t back = seen.size();
            std::cout << h[v].index << " " << vertex_name(v)
                      << " chars=" << h[v].char_reach.count()
                      << " indeg=" << in_degree(v, h)
                      << " outdeg=" << out_degree(v, h)
                      << " fwd_reach=" << fwd
                      << " back_reach=" << back << "\n";
        }

        // Regions
        auto regions = assignRegions(h);
        std::map<u32, std::vector<NFAVertex>> reg_nodes;
        for (auto v : vertices_range(h)) {
            reg_nodes[regions[v]].push_back(v);
        }
        std::cout << "\nRegions: " << reg_nodes.size() << "\n";
        for (const auto &m : reg_nodes) {
            u32 rid = m.first;
            const auto &vec = m.second;
            std::cout << " region " << rid << " size=" << vec.size();
            std::cout << " entries:";
            for (auto v : vec) {
                if (isRegionEntry(h, v, regions)) std::cout << h[v].index << ",";
            }
            std::cout << " exits:";
            for (auto v : vec) {
                if (isRegionExit(h, v, regions)) std::cout << h[v].index << ",";
            }
            std::cout << "\n";
        }

        // Region graph edges (counts)
        std::map<std::pair<u32,u32>, size_t> region_edges;
        for (auto e : edges_range(h)) {
            u32 ru = regions[source(e, h)];
            u32 rv = regions[target(e, h)];
            if (ru != rv) region_edges[std::make_pair(ru, rv)]++;
        }
        std::cout << "\nRegion edges:\n";
        for (const auto &p : region_edges) {
            std::cout << "  " << p.first.first << " -> " << p.first.second
                      << " count=" << p.second << "\n";
        }

        // Netflow / min-cut (operate on a clone)
        auto hcopy = cloneHolder(h);
        if (hcopy) {
            NGHolder &hc = *hcopy;
            auto scores = scoreEdges(hc);
            auto cut = findMinCut(hc, scores);
            std::unordered_set<size_t> cut_idx;
            u64a cut_sum = 0;
            for (auto e : cut) {
                cut_idx.insert(hc[e].index);
                if (hc[e].index < scores.size()) cut_sum += scores[hc[e].index];
            }
            std::cout << "\nMin-cut edges (sum scores=" << cut_sum << "):\n";
            for (auto e : cut) {
                std::cout << "  e" << hc[e].index << " (" << hc[source(e, hc)].index
                          << "," << hc[target(e, hc)].index << ") score="
                          << (hc[e].index < scores.size() ? scores[hc[e].index] : 0)
                          << "\n";
            }

            // Partition: reachable from source without crossing cut edges
            std::unordered_set<NFAVertex> sset;
            std::queue<NFAVertex> q2;
            q2.push(hc.start);
            sset.insert(hc.start);
            while (!q2.empty()) {
                NFAVertex u = q2.front(); q2.pop();
                for (const auto &e : out_edges_range(u, hc)) {
                    if (cut_idx.count(hc[e].index)) continue;
                    NFAVertex w = target(e, hc);
                    if (sset.insert(w).second) q2.push(w);
                }
            }
            std::cout << "\nPartition sizes: S=" << sset.size() << " T="
                      << (num_vertices(hc) - sset.size()) << "\n";
        }

        std::cout << "\n--- Decomposition (recursive) ---\n";
        decomposeGraphRec(h, 0, 3);

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 3;
    }

    return 0;
}
