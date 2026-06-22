# Rose Execution Flow (Block Mode)

The following pseudocode outlines the execution of a block of data through the Rose engine, referencing the exact lines in `src/rose/block.c` where these operations occur.

```c
// Entry point for scanning a contiguous block of memory
function roseBlockExec(RoseEngine t, scratch) { // src/rose/block.c:345

    // 1. Initialize State
    // Clears the runtime state, queues, and prepares prefix/outfix engines
    init_for_block(t, scratch, state, is_small_block); // src/rose/block.c:378

    // 2. Fast Path: Small Block Optimization
    if (is_small_block) { // src/rose/block.c:382
        // If the block is very small, run a single coalesced HWLM (Hardware Literal Matcher) scan
        sbtable = getSBLiteralMatcher(t); // src/rose/block.c:383
        hwlmExec(sbtable, ...); // src/rose/block.c:390
    } 
    // 3. Normal Path: Anchored and Floating Matchers
    else {
        // Run any prefix NFAs that are marked as 'eager' (they run regardless of literals)
        runEagerPrefixesBlock(t, scratch); // src/rose/block.c:393
        
        // Execute the anchored literal table (literals tied to the start of the buffer)
        if (roseBlockAnchored(t, scratch)) { // src/rose/block.c:395
            return; // Halt if matching was instructed to stop
        }
        
        // Execute the floating literal table (general substrings)
        // Uses high-speed engines like Teddy or Noodle to scan the buffer
        if (roseBlockFloating(t, scratch)) { // src/rose/block.c:398
            return; 
        }
    }

    // 4. Resolve Pending Matches
    // Flushes any literal matches that were delayed due to history requirements
    if (cleanUpDelayed(t, scratch, length, 0) == HWLM_TERMINATE_MATCHING) { // src/rose/block.c:403
        return;
    }

    // 5. Execute Suffixes and Outfixes
    // Runs stateful NFAs (suffixes/outfixes) over the data up to the current offset
    roseCatchUpTo(t, scratch, length); // src/rose/block.c:409

    // 6. End of Data (EOD) Processing
    // If the pattern has anchors to the end of the buffer ($ or \z), run the EOD verification program
    if (t->requiresEodCheck && t->eodProgramOffset) { // src/rose/block.c:411
        roseBlockEodExec(t, length, scratch); // src/rose/block.c:421
    }
}
```

### Explanation of Key Steps

1. **`roseBlockAnchored` / `roseBlockFloating`**: This is where the hardware scanner actually scans for literals. If a literal is found, a callback is fired (`roseFloatingCallback` or `roseAnchoredCallback`).
2. **The Callbacks**: Inside the callback, the Rose engine verifies bounds (`minBound`, `maxBound`) and validates the `left` (infix) engines.
3. **`roseCatchUpTo`**: Suffix engines (NFAs that follow a matched literal) are scheduled and executed in this step, running forward through the data stream.
