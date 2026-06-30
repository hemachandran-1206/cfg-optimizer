# CFG Optimizer — C Code to Optimized Control Flow Graph

> A Clang/LLVM-based compiler tool that extracts Control Flow Graphs (CFGs) from C code, performs data-flow analysis, and applies four classic compiler optimizations with visual output.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture & Pipeline](#architecture--pipeline)
3. [Phase 1 — CFG Extraction](#phase-1--cfg-extraction)
4. [Phase 2 — Data-Flow Analysis](#phase-2--data-flow-analysis)
5. [Phase 3 — Optimizations](#phase-3--optimizations)
6. [Dashboard](#dashboard)
7. [Build & Run](#build--run)
8. [Project Structure](#project-structure)
9. [Contributors](#contributors)
10. [License](#license)

---

## Overview

This tool takes ordinary C source code and performs a **3-phase compiler pipeline** plus a **web dashboard**:

| Phase | Purpose | Output |
|-------|---------|--------|
| **Phase 1** | Parse C code → Build CFG | `*_cfg.png` — visual control-flow graph |
| **Phase 2** | Data-flow analysis | Terminal printout of reaching definitions & live variables |
| **Phase 3** | Apply 4 optimizations | `*_cfg_before.png`, `*_cfg_after.png`, `*_opt_summary.png`, `*_optimized.c` |
| **Dashboard** | Web UI for interactive use | Browser-based code editor + result viewer |

### Four Optimizations Applied

1. **Constant Folding (CF)** — Evaluate compile-time expressions at analysis time
2. **Constant Propagation (CP)** — Replace variables with proven constant values
3. **Dead Code Elimination (DCE)** — Remove assignments that are never read
4. **Unreachable Code Removal (UCR)** — Delete blocks that can never execute

---

## Architecture & Pipeline

```
┌─────────────┐    ┌─────────────┐    ┌─────────────────┐    ┌──────────────┐
│   C Code    │───▶│  Clang AST  │───▶│  CFG::buildCFG  │───▶│  Phase 1     │
│  (input.c)  │    │   Parser    │    │  (LLVM API)     │    │  PNG Output  │
└─────────────┘    └─────────────┘    └─────────────────┘    └──────────────┘
                                                                       │
                                                                       ▼
┌────────────────────────────────────────────────────────────────────────────┐
│                              Phase 2 — Data-Flow                             │
│  ┌─────────────────────┐      ┌─────────────────────┐                     │
│  │ Reaching Definitions │      │ Live Variable Analysis│                     │
│  │ Forward flow         │      │ Backward flow        │                     │
│  │ IN[B] = ⋃ OUT[P]     │      │ LIVE_IN = USE ∪      │                     │
│  │ OUT = GEN ∪ (IN-KILL)│      │          (LIVE_OUT-DEF)                  │
│  └─────────────────────┘      └─────────────────────┘                     │
└────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌────────────────────────────────────────────────────────────────────────────┐
│                              Phase 3 — Optimize                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────────────────┐  │
│  │ Constant    │  │ Constant    │  │ Dead Code   │  │ Unreachable Code │  │
│  │ Folding     │  │ Propagation │  │ Elimination │  │ Removal          │  │
│  └─────────────┘  └─────────────┘  └─────────────┘  └──────────────────┘  │
│                                                                             │
│  Output: before/after CFG PNGs + summary PNG + optimized `.c` file         │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## Phase 1 — CFG Extraction

### What It Does

Clang parses the C file into an **Abstract Syntax Tree (AST)**. For each function, we call `CFG::buildCFG()` to construct a **Control Flow Graph** where:

- Each **node** = a **basic block** (a sequence of statements executed sequentially)
- Each **edge** = a possible **transfer of control** between blocks (e.g., after a conditional)

### Example: Simple Function

```c
int max(int a, int b) {
    if (a > b)          // Block 1: condition
        return a;       // Block 2: true branch
    else
        return b;       // Block 3: false branch
}
```

**Generated CFG:**

```
    ┌──────┐
    │ENTRY │
    └──┬───┘
       │
       ▼
    ┌──────────┐
    │ Block 1  │  a > b
    │ [cond]   │
    └───┬──┬───┘
   true│  │false
       ▼  ▼
   ┌────┐ ┌────┐
   │B2  │ │B3  │  return a    return b
   │ret │ │ret │
   └──┬─┘ └──┬─┘
      │      │
      ▼      ▼
    ┌──────────┐
    │   EXIT   │
    └──────────┘
```

### Implementation Details

- Uses `RecursiveASTVisitor` to traverse every `FunctionDecl`
- `CFG::buildCFG(F, body, Context, BuildOptions())` creates the graph
- Each block's statements are pretty-printed via `Stmt::printPretty()`
- Output is a **DOT file** rendered by Graphviz `dot -Tpng`
- Colors: **green** = ENTRY, **red** = EXIT, **blue** = normal block, **darkgreen/red** = true/false edges

---

## Phase 2 — Data-Flow Analysis

### 2A — Reaching Definitions (Forward Analysis)

**Goal:** For each basic block, determine which variable definitions (assignments) can "reach" it from the entry.

**Key Concepts:**
- **GEN[B]** = Definitions generated (created) inside block B
- **KILL[B]** = Definitions killed (overwritten) inside block B
- **IN[B]** = Definitions reaching the start of block B
- **OUT[B]** = Definitions reaching the end of block B

**Equations:**
```
IN[B]  = ⋃ OUT[P]    for all predecessors P of B
OUT[B] = GEN[B] ∪ (IN[B] − KILL[B])
```

**Iterative Algorithm:**
```python
# Initialize IN[B] = ∅, OUT[B] = GEN[B] for all B
changed = True
while changed:
    changed = False
    for each block B:
        new_IN  = union of OUT[predecessors]
        new_OUT = GEN[B] ∪ (new_IN − KILL[B])
        if new_IN != IN[B] or new_OUT != OUT[B]:
            IN[B]  = new_IN
            OUT[B] = new_OUT
            changed = True
```

**Example:**
```c
int foo() {
    int x = 5;      // def: x@B1
    int y = x + 1;  // def: y@B1, use: x
    x = 10;         // def: x@B1 (kills previous x@B1)
    return x;       // use: x (reaches from x=10, not x=5)
}
```

| Block | GEN | KILL | IN | OUT |
|-------|-----|------|----|-----|
| B1 | x@B1, y@B1 | x@B1 (old) | ∅ | x@B1(new), y@B1 |

**Uninitialized Variable Check:** Any variable declared without initialization is flagged as a warning.

---

### 2B — Live Variable Analysis (Backward Analysis)

**Goal:** Determine which variables are "live" at each point — i.e., their values may be used later before being redefined.

**Key Concepts:**
- **USE[B]** = Variables used in B before being defined in B
- **DEF[B]** = Variables defined (assigned) in B
- **LIVE_IN[B]** = Variables live at entry of B
- **LIVE_OUT[B]** = Variables live at exit of B

**Equations:**
```
LIVE_OUT[B] = ⋃ LIVE_IN[S]   for all successors S of B
LIVE_IN[B]  = USE[B] ∪ (LIVE_OUT[B] − DEF[B])
```

**Iterative Algorithm:**
```python
changed = True
while changed:
    changed = False
    for each block B:
        new_OUT = union of LIVE_IN[successors]
        new_IN  = USE[B] ∪ (new_OUT − DEF[B])
        if new_IN != LIVE_IN[B] or new_OUT != LIVE_OUT[B]:
            update and set changed = True
```

**Example:**
```c
int bar() {
    int a = 1;       // DEF: {a}
    int b = a + 2;   // USE: {a}, DEF: {b}
    int c = b * 3;   // USE: {b}, DEF: {c}
    return c;        // USE: {c}
}
```

| Block | USE | DEF | LIVE_OUT | LIVE_IN |
|-------|-----|-----|----------|---------|
| init | a | a,b | {c} | {a,b} → but DEF kills, so after: {b} → ... |

**Dead Assignment Detection:** If a variable is in `DEF[B]` but NOT in `LIVE_OUT[B]`, it's **dead** — assigned but never used again.

---

## Phase 3 — Optimizations

### Optimization 1: Constant Folding (CF)

**What:** If an expression contains only constants, evaluate it at compile time.

**Algorithm:** Walk the AST, find `BinaryOperator` where both LHS and RHS are `IntegerLiteral`. Evaluate using `EvaluateAsInt()`.

**Example:**
```c
// BEFORE
int a = 3 + 5;      // 3 and 5 are both constants
int b = 10 * 2;     // 10 and 2 are both constants

// AFTER (Phase 3 output)
int a = 8;          // folded: 3 + 5 = 8
int b = 20;         // folded: 10 * 2 = 20
```

**Supported operators:** `+`, `-`, `*`, `/`, `%`

---

### Optimization 2: Constant Propagation (CP)

**What:** If a variable has exactly ONE reaching definition and that definition assigns a constant, replace all uses with that constant.

**Algorithm:**
1. Compute reaching definitions (from Phase 2A)
2. For each variable use, check if all reaching definitions agree on the same constant value
3. If yes, replace the variable with the constant literal

**Example:**
```c
// BEFORE
int limit = 2 + 3;      // limit = 5 (also constant-folded)
int start = 0;          // start = 0
int i = start;          // i = 0  (propagate start)
while (i < limit) {     // i < 5  (propagate limit)
    sum = sum + i;
    i = i + 1;
}

// AFTER (Phase 3 output)
int limit = 5;
int start = 0;
int i = 0;
while (i < 5) {         // CP applied: limit → 5
    sum = sum + i;
    i = i + 1;
}
```

**Important:** CP is only safe when ALL reaching definitions agree. If a variable is redefined (e.g., `i++`), the multiple definitions prevent unsafe propagation.

---

### Optimization 3: Dead Code Elimination (DCE)

**What:** Remove assignments whose results are never read (not live after the assignment).

**Algorithm:**
1. Compute live variable analysis (from Phase 2B)
2. For each assignment in a block: if the defined variable is NOT in `LIVE_OUT`, and NOT used later in the same block → it's **dead**

**Example:**
```c
// BEFORE
int dead() {
    int unused = 42;    // assigned but never read
    int waste = 99;     // assigned but never read
    int result = 7;     // will be returned = LIVE
    return result;
}

// AFTER (Phase 3 output)
int dead() {
    // [REMOVED-DCE] int unused = 42;
    // [REMOVED-DCE] int waste = 99;
    int result = 7;
    return result;
}
```

**Why it works:** In `LVA`, `unused` and `waste` are in `DEF` but not in `LIVE_OUT` of their block. The `return` only needs `result`.

---

### Optimization 4: Unreachable Code Removal (UCR)

**What:** Delete basic blocks that can never be reached from the ENTRY block.

**Algorithm:**
1. BFS/DFS from ENTRY following successor edges
2. Mark all visited blocks as reachable
3. Any unvisited block is unreachable → removed

**Example:**
```c
// BEFORE
int unreachable() {
    int x = 5;
    if (0) {            // condition is ALWAYS false
        x = 999;        // this block is unreachable
    }
    return x;
}

// AFTER (Phase 3 output)
int unreachable() {
    int x = 5;
    // [REMOVED] Block 2 — unreachable, never executes
    return x;
}
```

**Another common case:** Code after `return` is unreachable:
```c
int after_return() {
    return 42;
    int x = 99;         // unreachable — never executes
    return x;
}
```

---

## Visual Output Legend

The `*_cfg_after.png` files use a color scheme to highlight optimizations:

| Color | Meaning | Code |
|-------|---------|------|
| 🟡 Yellow (`#FFFACD`) | **Constant Folding** applied | Block contains a folded expression |
| 🟠 Orange (`#FFE4B5`) | **Constant Propagation** applied | Variable replaced with constant |
| 🟤 Peach (`#FFDAB9`) | **Dead Code Elimination** | Block contains removed dead code |
| ⚪ Grey (`#D3D3D3`) | **Unreachable Block** | Block was removed |
| 🔵 Blue border | Multiple optimizations | Block has 2+ optimization types |

---

## Dashboard

### Web UI

A Flask-based interactive dashboard lets you paste C code and instantly see:

- **CFG Before** — control flow graph before optimization
- **CFG After** — optimized CFG with color highlights
- **Summary** — visual diagram of all 4 optimizations found
- **Optimized Code** — fully runnable, regenerated C code
- **Terminal** — raw analyzer output for debugging

### Architecture

```
┌────────────┐      POST /optimize      ┌────────────┐
│  Browser   │ ──────────────────────▶ │  Flask     │
│  (UI)      │  {code: "int main()..."}│  (app.py)  │
└────────────┘                         └─────┬──────┘
       │                                    │
       │  JSON response                     │  spawn process
       │  {functions, optimized_code,     │
       │   terminal, before/after/summary}  │
       │                                    ▼
       │                            ┌────────────┐
       │                            │ Phase 3    │
       │                            │ Binary     │
       │                            └─────┬──────┘
       │                                  │
       │                                  │ generates
       │                                  ▼
       │                            ┌────────────┐
       │                            │ *.png,     │
       │                            │ *_optimized.c│
       │                            └────────────┘
       │                                  │
       └──────────────────────────────────┘
```

### Running the Dashboard

```bash
cd dashboard
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
python app.py
```

Open [http://localhost:5000](http://localhost:5000) and paste your C code.

---

## Build & Run

### Dependencies

- **LLVM/Clang 21** (libTooling, libAnalysis, libAST)
- **CMake** ≥ 3.16
- **Graphviz** (`dot` command)
- **Python 3** + **Flask**
- **g++** or **clang++** with C++17

```bash
# Ubuntu / Debian
sudo apt-get update
sudo apt-get install -y llvm-21 clang-21 libclang-21-dev cmake graphviz python3 python3-pip
pip3 install flask
```

> Adjust `llvm-21` to your installed version. Update `phase2/CMakeLists.txt` and `phase3/CMakeLists.txt` `PATHS` if needed.

### Build

```bash
# Phase 1
cd phase1/build && cmake .. && make

# Phase 2
cd ../../phase2/build && cmake .. && make

# Phase 3
cd ../../phase3/build && cmake .. && make
```

### Command-Line Usage

```bash
# Phase 1 — CFG only
./phase1/build/cfg_extractor -- datasets/testfile.c

# Phase 2 — Data-flow analysis
./phase2/build/cfg_extractor -- datasets/testfile.c

# Phase 3 — Full optimization pipeline
./phase3/build/cfg_extractor -- datasets/test_allphase.c

# Output files generated:
#   compute_cfg_before.png
#   compute_cfg_after.png
#   compute_opt_summary.png
#   compute_optimized.c
```

---

## Project Structure

```
cfg-optimizer/
├── .gitignore              # Excludes build artifacts, venv, generated files
├── README.md               # This file
├── phase1/                 # CFG extraction (Clang libTooling)
│   ├── cfg_extractor.cpp
│   ├── CMakeLists.txt
│   └── build/
├── phase2/                 # Data-flow analysis
│   ├── cfg_extractor.cpp
│   ├── CMakeLists.txt
│   └── build/
├── phase3/                 # Optimizations + code generation
│   ├── cfg_extractor.cpp
│   ├── CMakeLists.txt
│   └── build/
├── dashboard/              # Flask web UI
│   ├── app.py
│   ├── requirements.txt
│   ├── static/
│   │   ├── style.css
│   │   └── output/         # Pre-generated sample PNGs
│   └── templates/
│       └── index.html
└── datasets/               # Sample C test files
    ├── test_allphase.c     # Stress test: all 4 optimizations
    ├── test_deadcode.c     # Dead code elimination demo
    ├── test_unreachable.c  # Unreachable code demo
    ├── insersionsort.c     # Algorithm with loops
    ├── mergesort.c
    ├── quicksort.c
    └── kadanesalgo.c
```

---

## How It Works (Deep Dive)

### 1. Clang Integration

All three phases use **Clang libTooling** (`CommonOptionsParser` + `ClangTool`) to:
- Parse C source into AST
- Walk the AST with `RecursiveASTVisitor`
- Build CFG via `CFG::buildCFG()`

### 2. Why Reverse Post-Order (RPO) Matters

Clang assigns block IDs in **reverse post-order**. For code generation, we sort blocks in RPO to produce C code that roughly matches the original source order:

```cpp
std::vector<CFGBlock*> reversePostOrder(CFG *cfg) {
    // DFS from ENTRY, collect post-order, then reverse
    // Unreachable blocks appended at the end (commented out)
}
```

### 3. Statement Classification

To avoid emitting invalid C, we classify statements:

| Type | Action | Example |
|------|--------|---------|
| `ImplicitCastExpr`, `ParenExpr` | Skip (internal AST node) | `(int)x` wrapper |
| `IntegerLiteral`, `FloatingLiteral` | Skip (sub-expression) | `return 0;` value |
| `IfStmt`, `WhileStmt`, `ForStmt` | Skip as standalone (structure via edges) | `if (cond)` |
| `ReturnStmt` | Keep, but skip its return-value sub-expression | `return x;` |
| `BinaryOperator(=)`, `UnaryOperator(++)` | Keep with semicolon | `a = b;` |

### 4. Semicolon Handling

The `needsSemicolon()` function prevents double semicolons:
- `return 0;` already ends with `;` → don't add another
- `if (x)` is a control-flow statement → no semicolon
- `a = b` needs `;` appended

---

## Contributors

| Name | Role | GitHub |
|------|------|--------|
| **Hemachandran** | Project Lead, Architecture & Implementation | [@hemachandran-1206](https://github.com/hemachandran-1206) |
| **Hemanath** | Collaborator — Data-flow analysis & Testing | [@hemanath-22](https://github.com/hemanath-22) |
| **Bilal Shaik** | Collaborator — Dashboard & Visualization | [@iambilalshaik](https://github.com/iambilalshaik) |

---

## License

MIT License — feel free to use, modify, and distribute.

---

> Built with Clang/LLVM, Graphviz, and Flask.
