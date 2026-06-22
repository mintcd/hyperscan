# Structure and Execution of the Rose Graph

In the Hyperscan architecture, **Rose** is the top-level orchestrating engine that glues together fast literal matching (via engines like FDR, HWLM) and more complex stateful matching (via NFA engines like DFA, Castle, and Limex). 

The `RoseGraph` is a directed graph structure built using the Boost Graph Library (BGL), specifically defined in `src/rose/rose_graph.h`.

## 1. Graph Structure

The `RoseGraph` is composed of **Vertices** (Roles) and **Edges** (Transitions/Bounds):

### Vertices (`RoseVertexProps`)
Each vertex in the graph typically represents a **Role**, which often correlates to a string literal (or a set of literals) within the original regular expression.

Key properties include:
- **`literals`**: A set of literal IDs that, when matched by the hardware matcher, trigger this vertex.
- **`left` (`LeftEngInfo`)**: Information about any prefix or infix engine (e.g., NFAs) located to the left of this role. It dictates what complex patterns must match before the literal is considered valid.
- **`suffix` (`RoseSuffixInfo`)**: Information about any NFA suffix engine that needs to be triggered after this literal matches.
- **`min_offset` / `max_offset`**: Depth bounds verifying if the literal was found at a valid string offset.
- **`reports`**: A list of Report IDs to fire if this vertex constitutes a successful match.
- **`eod_accept`**: A flag indicating a virtual vertex that fires reports only at End-Of-Data (EOD).

### Edges (`RoseEdgeProps`)
Edges describe the constraints and transitions between one Role and another.
- **`minBound` / `maxBound`**: Define the valid distance (in bytes) between the end of the source literal match and the start of the target literal match.
- **`rose_top`**: Defines the "top" state to trigger on the target role's left engine.
- **`history`**: Encodes special matching history requirements (e.g., fixed offset history, EOD history).

## 2. Usage During Execution

During the execution phase (`src/rose/rose.c`), the compiled `RoseGraph` is translated into runtime bytecodes and flat structures. The matching process works broadly as follows:

1. **Hardware Literal Matching**: High-speed scanners (like Teddy or Noodle) rapidly search the input stream for any of the literals associated with the Rose vertices.
2. **Role Verification**: When a literal matches, the Rose engine wakes up and inspects the corresponding Role.
3. **Bound Checking**: It immediately checks if the match occurred within the `min_offset` and `max_offset`.
4. **Graph Traversal (State Checks)**: It checks incoming edges to ensure that the required predecessor roles have matched at the correct distances (`minBound`, `maxBound`). If the role requires a prefix or infix engine (`left`), the NFA states are evaluated to confirm validity.
5. **Triggering Successors**: If the current Role is fully validated, the engine updates its internal state (often utilizing scratch space) so that downstream target roles can succeed. It may also wake up suffix engines (`suffix`).
6. **Reporting**: If the Role contains `reports`, the matching callbacks are fired back to the user application.

By splitting regexes into discrete literal "roles" connected by bounded distance edges and NFAs, Rose enables Hyperscan to avoid running slow NFA engines constantly, relying instead on high-speed literal scanners until a likely match area is encountered.
