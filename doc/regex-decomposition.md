# Regex / NFA Decomposition

This diagram summarizes the main steps Hyperscan uses to decompose an NFA (NGHolder) into LHS/RHS pieces and to extract useful literals for Rose.

```mermaid
flowchart LR
  %% Input
  A["Rose graph (NGHolder)"] --> B["getCandidatePivots (findDominators)"]

  %% Split selection
  B --> C["findBestSplit"]
  C --> C1["getSimpleRoseLiterals"]
  C --> C2["getRegionRoseLiterals"]

  %% Literal extraction & scoring
  C1 --> L["getLiteralSet"]
  C2 --> L
  L --> W["processWorkQueue"]
  W --> X["compressAndScore"]
  X --> Xcut["LitGraph min-cut"]

  %% Prefix handling
  C --> Pfx["findBestPrefixSplit"]
  Pfx --> Pfx2["findSimplePrefixSplit"]

  %% Netflow (fallback / wider cut)
  C --> N["doNetflowCut"]
  N --> N1["poisonEdges / poisonFromSuccessor"]
  N --> S["scoreEdges"]
  S --> L
  N --> NF["findMinCut (netflow)"]
  NF --> SC["splitEdgesByCut"]

  %% Direct splitting path
  C --> R["splitRoseEdge"]
  R --> Match["can_match"]
  R --> SG["splitGraph"]
  SG --> LHS["splitLHS"]
  SG --> RHS["splitRHS"]

  %% Wiring the outputs
  SC --> LHS
  SC --> RHS

  subgraph Literal_analysis
    direction TB
    L
    W
    X
    Xcut
  end

  subgraph Netflow_MinCut
    direction TB
    N
    S
    NF
    N1
  end

  subgraph Splitting_cloning
    direction TB
    R
    SG
    LHS
    RHS
    Match
    SC
  end

```

Legend: node labels show the function name; where useful the originating source (for example: ng_violet.cpp, ng_split.cpp, ng_literal_analysis.cpp, ng_netflow.cpp) maps to stages in the diagram.

Files referenced in this repo:
- src/nfagraph/ng_violet.cpp — orchestration, findBestSplit, splitRoseEdge, doNetflowCut.
- src/nfagraph/ng_split.cpp — low-level splitGraph, splitLHS, splitRHS.
- src/nfagraph/ng_literal_analysis.cpp — getLiteralSet, compressAndScore.
- src/nfagraph/ng_netflow.cpp — findMinCut (netflow).

Next: I can render this Mermaid diagram to SVG for preview, or embed it in the repo README; which do you prefer?
