# "Thin Vanilla" Design Principles

## Philosophy

When extending an upstream codebase (vanilla Quake II, a game DLL, a client port, etc.), follow a **thin vanilla** architecture:

- Keep changes to upstream code minimal and intentional.
- Place new feature logic in a dedicated mod folder (e.g. `ozbot/`, `mod/`).
- Maintain a clean boundary between upstream code and extension code.
- Make extension features easy to find, test, disable, or remove.

## Why This Matters

### 1. Maintainability
- Upstream updates merge with fewer conflicts.
- Extension features stay self-contained and easier to reason about.
- Ownership is clearer: "core upstream" vs "mod behavior."

### 2. Debuggability
- Regressions are easier to isolate (upstream hook vs extension logic).
- Features can be toggled for verification and rollback.
- Change origin is explicit through standardized markers.

### 3. Upstream Respect
- Avoids spreading project-specific logic through baseline files.
- Keeps upstream files readable for contributors and reviewers.
- Reduces long-term maintenance friction.

### 4. Modularity
- Features can be enabled/disabled with cvars or config gates.
- Independent features can be removed without broad refactors.
- Functionality is more portable to future branches or forks.

## Implementation Guidelines

### ✅ DO: Keep Upstream Hooks Small

**Good Example** — add a single toggle check:
```cpp
// In g_main.cpp (upstream file)
// [Ozbot] Map rotation control toggle
cvar_t *mod_map_shuffle_once;

// [Ozbot] Skip re-shuffle when one-time shuffle is active
if (g_map_list_shuffle->integer && !mod_map_shuffle_once->integer)
{
    // Existing upstream shuffle code
}
```

**Why this is good:**
- Adds only a declaration and a focused conditional hook.
- Preserves upstream signatures and control flow.
- Uses clear project markers (e.g. `[Ozbot]`) for fast auditing.

### ✅ DO: Put Behavior in Extension Modules

**Good Example** — implementation in the mod folder:
```cpp
// In ozbot/mod_main.cpp (extension file)
namespace {
    std::vector<std::string> split_map_list(std::string_view input, char delim) {
        // Local helper logic
    }
}

void Mod_ShuffleMapListOnce() {
    auto values = split_map_list(g_map_list->string, ' ');
    std::shuffle(values.begin(), values.end(), mt_rand);
    // ... rest of extension behavior
}
```

**Why this is good:**
- Complex logic stays out of upstream files.
- Helpers remain local to the feature module.
- Reuses engine/upstream infrastructure with minimal coupling.

### ✅ DO: Reuse Existing Infrastructure

**Good Example** — avoid duplicate utilities:
```cpp
extern std::mt19937 mt_rand;          // Existing RNG
gi.cvar_set("g_map_list", "...");     // Existing cvar system
join_strings(values, " ");            // Existing utility helper
```

**Why this is good:**
- Reduces duplicated logic and drift risk.
- Keeps behavior consistent with existing systems.
- Minimizes code surface area.

### ❌ DON'T: Move Feature Bodies into Upstream Files

**Bad Example:**
```cpp
// In g_main.cpp (upstream file)
void ShuffleMapListOnce() {
    // 50 lines of mod-specific behavior
    // Should live in ozbot/ (or your mod folder)
}
```

**Why this is bad:**
- Blurs boundaries between upstream and extension code.
- Increases merge conflicts and review cost.
- Makes rollback/removal harder.

### ❌ DON'T: Change Upstream Signatures for Mod Needs

**Bad Example:**
```cpp
// In g_main.cpp
void EndDMLevel(bool mod_skip_shuffle)  // BAD
{
    // ...
}
```

**Why this is bad:**
- Creates broad API churn for a local feature.
- Forces unrelated call-site changes.
- Complicates future upstream sync.

### ❌ DON'T: Reimplement Existing Helpers

**Bad Example:**
```cpp
// In ozbot/mod_main.cpp
namespace {
    std::string join_strings(...) { }  // BAD: duplicate helper
}
```

**Why this is bad:**
- Introduces duplicate behavior paths.
- Increases inconsistency risk over time.
- Expands maintenance overhead.

## Code Organization

### Preferred Layout
```text
src/
|-- g_*.cpp              # Upstream files (minimal extension hooks)
|-- p_*.cpp              # Upstream files (minimal extension hooks)
|-- m_*.cpp              # Upstream files (minimal extension hooks)
`-- ozbot/               # Extension-specific code (use your project name)
    |-- ozbot.h          # Public extension API
    |-- mod_main.cpp     # Core systems
    |-- mod_nav.cpp      # Navigation features
    |-- mod_bot.cpp      # Bot behavior
    `-- mod_*.cpp        # Other extension modules
```

### Change Marking Standard

Always annotate upstream edits with a consistent project tag:
```cpp
// [Ozbot] Brief why + what
code_here();

// [Ozbot] Feature gate
if (mod_feature_enabled->integer) {
    Mod_DoSomething();
}
```

Pick one tag per project (`[Ozbot]`, `[Mod]`, etc.) and use it everywhere.

## Practical Example: One-Time Map Shuffle

### Upstream-side touch points
1. Cvar declaration/init
2. One conditional hook before existing shuffle behavior
3. One call into the extension implementation

**Target footprint:** single-digit line changes in upstream files.

### Extension-side implementation
1. Local helper(s), if needed
2. Main feature function in the mod folder
3. Optional debug logging behind a cvar gate

**Target footprint:** the bulk of logic lives in extension files.

### Outcome
- Upstream stays clean and reviewable.
- Extension behavior stays modular and testable.
- Feature rollback is low-risk.

## Checklist for New Extension Features

Before merging a feature:
- [ ] Is most logic implemented in the mod folder?
- [ ] Are upstream edits minimal and clearly marked with the project tag?
- [ ] Are upstream changes hooks/config gates, not feature bodies?
- [ ] Are existing utilities reused instead of duplicated?
- [ ] Can the feature be disabled cleanly via cvar/config?
- [ ] Will upstream sync remain straightforward?
- [ ] Is behavior documented for future maintainers?

## Anti-Patterns to Avoid

### The "Swiss Army Upstream"
Packing feature logic into upstream files because it feels faster short-term.

### The "Duplicate Helper"
Rewriting utilities that already exist in shared/upstream code.

### The "Silent Invader"
Editing upstream files without explicit project annotations.

### The "Signature Breaker"
Changing upstream function signatures for mod-specific flags or data.

### The "Mandatory Feature"
Shipping behavior with no clean off-switch or rollback path.

## Decision Rule

When unsure, ask:
**"Can this stay in the mod folder with only a tiny upstream hook?"**

If the answer is "yes," do that.
