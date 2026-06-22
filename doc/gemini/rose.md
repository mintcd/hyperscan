# Rose Engine: Code to Abstraction Mapping

This document provides a direct mapping between the C++ implementation of the Hyperscan Rose engine and a theoretical algorithmic abstraction. This serves as a foundational reference for analyzing the Rose engine using a flattened, bipartite-like graph of Literals and Finite Automata (FAs).

## 1. The C++ Implementation (The "Compressed" Graph)

In the actual Hyperscan codebase, the graph is literal-centric. FAs are not independent nodes; they are properties of the literal vertices they precede or follow.

### Data Structures (`src/rose/rose_graph.h`)
* **`RoseGraph`** (Line 222): The core directed graph built on the Boost Graph Library (`ue2_graph`).
* **`RoseVertexProps`** (Line 140): Represents a "Role". It defines the literal to be matched and embeds the surrounding state machines.
  * `literals` (Line 145): The specific string(s) to scan for.
  * `left` (Line 169): A `LeftEngInfo` struct (Line 79) containing the prefix or infix FA (e.g., an NFA or DFA) that must match *before* this literal.
  * `suffix` (Line 177): A `RoseSuffixInfo` struct (Line 121) containing the FA that executes *after* this literal matches.
* **`RoseEdgeProps`** (Line 185): Represents the conditions to transition between Roles.
  * `minBound` / `maxBound` (Lines 196, 204): Positional constraints between literals.
  * `rose_top` (Line 208): The specific state to trigger in the target vertex's `left` FA when the edge is traversed.

### Execution Flow (`src/rose/block.c` & `src/rose/catchup.c`)
1. **Fast Literal Scanning**: `roseBlockFloating` (`src/rose/block.c:221`) calls `hwlmExec` to scan the data purely for literals.
2. **Lazy Verification**: When a literal matches, the fast scanner pauses and triggers a callback. The engine calls `roseCatchUpTo` (`src/rose/block.c:409`) to execute the FAs. It retroactively runs the `left` FA over the skipped bytes to verify if the algorithmic precedents were actually satisfied.

---

## 2. The Algorithmic Abstraction (The "Unrolled" Graph)

For mathematical and algorithmic analysis, the compressed C++ structs can be unrolled into a flat graph where **every component is a distinct vertex**, and **edges dictate strict positional and temporal dependencies**.

### Abstraction Definitions
Let $G = (V, E)$ be the dependency graph.

**Vertices ($V$)**: 
A vertex $v \in V$ is strictly either:
1. **$V_{Lit}$ (Literal Vertex)**: Represents an exact string match (e.g., `ab`). 
   * *Code Reference*: Maps to `RoseVertexProps::literals`.
2. **$V_{FA}$ (Finite Automaton Vertex)**: Represents a state machine evaluating a regular expression (e.g., `[0-9]+`).
   * *Code Reference*: Maps to `LeftEngInfo::graph` or `RoseSuffixInfo::graph` inside `RoseVertexProps`.

**Edges ($E$)**:
An edge $e = (u, v)$ represents a strict dependency: $v$ can only be evaluated if $u$ has matched, and $v$ must satisfy the positional conditions relative to $u$.
* *Code Reference*: Maps directly to `RoseEdgeProps` (`minBound`, `maxBound`, `rose_top`).

### Abstract Execution Rule
The fundamental matching rule for this abstract graph is:
> **Rule**: A vertex $v$ matches at interval $[i, j)$ if and only if all predecessor vertices $u \in \text{predecessors}(v)$ have matched, and the positional distance between $u$'s match and $v$'s match satisfies the edge condition $E(u, v)$.

### Isomorphic Mapping Example
Given the regex `/ab[0-9]+cd/`:

**C++ Code Structure:**
`Vertex 1 (Literal: ab)` $\xrightarrow[\text{rose\_top}]{\text{minBound}}$ `Vertex 2 (Literal: cd, Left FA: [0-9]+)`

**Algorithmic Abstraction Structure:**
`Node 1 (Literal: ab)` $\xrightarrow{\text{pos}}$ `Node 2 (FA: [0-9]+)` $\xrightarrow{\text{pos}}$ `Node 3 (Literal: cd)`

By utilizing this abstraction, complex NFA state analysis and literal bounds checking can be modeled as standard DAG traversal with positional constraints, fully preserving the logical correctness of the underlying `roseCatchUpTo` and `LeftEngInfo` mechanics.