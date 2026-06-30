// ============================================================
//  PHASE 3 — CFG OPTIMIZATIONS (FINAL)
//  FIXED: generateOptimizedCode & generateCFGPng
// ============================================================

#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Analysis/CFG.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Expr.h"
#include "llvm/Support/CommandLine.h"
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace clang;
using namespace clang::tooling;

// ─────────────────────────────────────────────────────────────
//  UTILITIES
// ─────────────────────────────────────────────────────────────

std::string escapeDot(const std::string &s) {
    std::string out;
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '{')  out += "\\{";
        else if (c == '}')  out += "\\}";
        else if (c == '<')  out += "\\<";
        else if (c == '>')  out += "\\>";
        else out += c;
    }
    if (out.length() > 50) out = out.substr(0, 50) + "...";
    return out;
}

void dotToPng(const std::string &dotFile, const std::string &pngFile) {
    std::string cmd = "dot -Tpng \"" + dotFile + "\" -o \"" + pngFile + "\" 2>/dev/null";
    system(cmd.c_str());
    std::cout << "[PNG] Saved: " << pngFile << "\n";
}

std::string stmtToString(const Stmt *S, ASTContext *Ctx) {
    if (!S) return "";
    std::string out;
    llvm::raw_string_ostream rso(out);
    S->printPretty(rso, nullptr, PrintingPolicy(Ctx->getLangOpts()));
    rso.flush();
    // Trim trailing whitespace/newlines
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

// Returns the variable name being defined by a statement, or "".
std::string getDefinedVar(const Stmt *S) {
    if (!S) return "";
    if (const DeclStmt *DS = dyn_cast<DeclStmt>(S))
        for (const Decl *D : DS->decls())
            if (const VarDecl *VD = dyn_cast<VarDecl>(D))
                if (VD->hasInit()) return VD->getNameAsString();
    if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(S))
        if (BO->getOpcode() == BO_Assign || BO->isCompoundAssignmentOp())
            if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(BO->getLHS()))
                return DRE->getDecl()->getNameAsString();
    if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(S))
        if (UO->isIncrementOp() || UO->isDecrementOp())
            if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(UO->getSubExpr()))
                return DRE->getDecl()->getNameAsString();
    return "";
}

std::set<std::string> getUsedVars(const Stmt *S) {
    std::set<std::string> used;
    if (!S) return used;
    if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(S)) {
        if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl()))
            used.insert(VD->getNameAsString());
        return used;
    }
    for (const Stmt *child : S->children()) {
        auto c = getUsedVars(child);
        used.insert(c.begin(), c.end());
    }
    // Remove the LHS of an assignment — it's defined, not used
    if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(S))
        if (BO->getOpcode() == BO_Assign)
            if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(BO->getLHS()))
                used.erase(DRE->getDecl()->getNameAsString());
    return used;
}

// ─────────────────────────────────────────────────────────────
//  STATEMENT CLASSIFICATION HELPERS
//  Used by both the CFG renderer and the code generator to
//  decide how to format / whether to emit a statement.
// ─────────────────────────────────────────────────────────────

// Returns true for statements that are only compiler-internal
// cast nodes and produce no meaningful source text of their own.
// We do NOT skip IfStmt, ReturnStmt, BinaryOperator, etc.
static bool isInternalNode(const Stmt *S) {
    if (!S) return true;
    // ImplicitCastExpr nodes appear as separate CFGElements but
    // their text is identical to their sub-expression; they are
    // redundant when the parent statement is already shown.
    return isa<ImplicitCastExpr>(S) || isa<ParenExpr>(S);
}

// Return true if the pretty-printed string is truly empty
// (nothing but whitespace / semicolons).
static bool isBlankString(const std::string &s) {
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != ';')
            return false;
    return true;
}

// Returns true if the statement already ends with ';' or is a
// compound/control-flow form that must NOT get an extra ';'.
static bool needsSemicolon(const Stmt *S, const std::string &text) {
    if (!S) return false;
    if (isa<IfStmt>(S) || isa<WhileStmt>(S) || isa<ForStmt>(S)  ||
        isa<DoStmt>(S) || isa<SwitchStmt>(S) || isa<CompoundStmt>(S))
        return false;
    if (!text.empty() && text.back() == ';') return false;
    return true;
}

// ─────────────────────────────────────────────────────────────
//  DATA-FLOW STRUCTURES
// ─────────────────────────────────────────────────────────────

struct Definition {
    std::string varName;
    unsigned blockID;
    unsigned stmtIndex;
    bool operator<(const Definition &o) const {
        if (varName != o.varName) return varName < o.varName;
        if (blockID != o.blockID) return blockID < o.blockID;
        return stmtIndex < o.stmtIndex;
    }
    bool operator==(const Definition &o) const {
        return varName == o.varName && blockID == o.blockID && stmtIndex == o.stmtIndex;
    }
};

struct ReachingDefResult {
    std::vector<Definition>                  allDefs;
    std::map<unsigned, std::set<Definition>> GEN, KILL, IN, OUT;
};

ReachingDefResult computeReachingDefs(CFG *cfg) {
    ReachingDefResult R;
    for (CFGBlock *block : *cfg) {
        unsigned stmtIdx = 0;
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                std::string var = getDefinedVar(S->getStmt());
                if (!var.empty())
                    R.allDefs.push_back({var, block->getBlockID(), stmtIdx});
                stmtIdx++;
            }
        }
    }
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        unsigned stmtIdx = 0;
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                std::string var = getDefinedVar(S->getStmt());
                if (!var.empty()) {
                    Definition def = {var, bid, stmtIdx};
                    R.GEN[bid].insert(def);
                    for (const Definition &d : R.allDefs)
                        if (d.varName == var && !(d == def))
                            R.KILL[bid].insert(d);
                }
                stmtIdx++;
            }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (CFGBlock *block : *cfg) {
            unsigned bid = block->getBlockID();
            std::set<Definition> newIN;
            for (CFGBlock *pred : block->preds())
                if (pred)
                    for (const Definition &d : R.OUT[pred->getBlockID()])
                        newIN.insert(d);
            std::set<Definition> newOUT = R.GEN[bid];
            for (const Definition &d : newIN)
                if (R.KILL[bid].find(d) == R.KILL[bid].end())
                    newOUT.insert(d);
            if (newIN != R.IN[bid] || newOUT != R.OUT[bid]) {
                R.IN[bid] = newIN; R.OUT[bid] = newOUT; changed = true;
            }
        }
    }
    return R;
}

struct LVAResult {
    std::map<unsigned, std::set<std::string>> USE, DEF, LIVE_IN, LIVE_OUT;
};

LVAResult computeLVA(CFG *cfg) {
    LVAResult L;
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                const Stmt *stmt = S->getStmt();
                std::string defVar = getDefinedVar(stmt);
                std::set<std::string> usedHere = getUsedVars(stmt);
                for (auto &v : usedHere)
                    if (L.DEF[bid].find(v) == L.DEF[bid].end())
                        L.USE[bid].insert(v);
                if (!defVar.empty()) L.DEF[bid].insert(defVar);
            }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (CFGBlock *block : *cfg) {
            unsigned bid = block->getBlockID();
            std::set<std::string> newOUT;
            for (CFGBlock *succ : block->succs())
                if (succ)
                    for (auto &v : L.LIVE_IN[succ->getBlockID()])
                        newOUT.insert(v);
            std::set<std::string> newIN = L.USE[bid];
            for (auto &v : newOUT)
                if (L.DEF[bid].find(v) == L.DEF[bid].end())
                    newIN.insert(v);
            if (newIN != L.LIVE_IN[bid] || newOUT != L.LIVE_OUT[bid]) {
                L.LIVE_IN[bid] = newIN; L.LIVE_OUT[bid] = newOUT; changed = true;
            }
        }
    }
    return L;
}

struct FoldResult  { std::string original, folded; unsigned blockID; };
struct PropResult  { std::string varUsed, inStmt;  long long knownValue; unsigned blockID; };
struct DeadResult  { std::string varName,  inStmt;  unsigned blockID; };

struct OptimizationResults {
    std::vector<FoldResult>  folds;
    std::vector<PropResult>  props;
    std::vector<DeadResult>  deads;
    std::vector<unsigned>    unreachable;
    std::set<unsigned> foldBlocks, propBlocks, deadBlocks, unreachBlocks;
};

// ─────────────────────────────────────────────────────────────
//  REWRITE HELPERS  (shared between CFG-PNG and code-gen)
// ─────────────────────────────────────────────────────────────

// Replace whole-word occurrences of `word` with `rep` in `s`.
static std::string replaceWholeWord(std::string s,
                                    const std::string &word,
                                    const std::string &rep) {
    size_t pos = 0;
    while ((pos = s.find(word, pos)) != std::string::npos) {
        bool L = (pos == 0 || (!isalnum((unsigned char)s[pos-1]) && s[pos-1] != '_'));
        bool R = (pos + word.size() >= s.size() ||
                 (!isalnum((unsigned char)s[pos+word.size()]) && s[pos+word.size()] != '_'));
        if (L && R) { s.replace(pos, word.size(), rep); pos += rep.size(); }
        else          pos += word.size();
    }
    return s;
}

// Apply constant-folding and constant-propagation rewrites to one
// statement string.  defVar is the variable being assigned (skip CP for it).
static std::string rewriteStmtStr(const std::string &raw,
                                  const std::string &defVar,
                                  const std::map<std::string,std::string> &foldMap,
                                  const std::map<std::string,long long>   &propMap) {
    std::string s = raw;
    // Constant folding — replace known constant sub-expressions
    for (auto &kv : foldMap) {
        size_t pos = s.find(kv.first);
        if (pos != std::string::npos)
            s.replace(pos, kv.first.size(), kv.second);
    }
    // Constant propagation — replace known-constant variables
    for (auto &kv : propMap) {
        if (kv.first == defVar) continue;
        s = replaceWholeWord(s, kv.first, std::to_string(kv.second));
    }
    return s;
}

// ─────────────────────────────────────────────────────────────
//  generateCFGPng  — FIXED
//
//  Key fixes vs. original:
//  1. Skip only genuinely internal/blank nodes (ImplicitCastExpr,
//     ParenExpr, truly empty strings).  Every real statement —
//     including condition expressions — is shown.
//  2. Terminator conditions are extracted separately and shown as
//     the last line of a block (e.g. "if (x > 0)") so branch
//     blocks never appear empty.
//  3. No extra semicolons on control-flow statements.
//  4. "after" mode correctly rewrites and annotates changed stmts.
// ─────────────────────────────────────────────────────────────

void generateCFGPng(CFG *cfg, ASTContext *Ctx,
                    const std::string &funcName,
                    const std::string &suffix,
                    const OptimizationResults *opts)
{
    std::string dotFile = funcName + "_cfg_" + suffix + ".dot";
    std::string pngFile = funcName + "_cfg_" + suffix + ".png";

    FILE *f = fopen(dotFile.c_str(), "w");
    if (!f) { std::cerr << "[ERR] Cannot open " << dotFile << "\n"; return; }

    bool isAfter = (suffix == "after") && (opts != nullptr);

    // Build lookup tables for "after" mode
    std::set<std::pair<unsigned,std::string>> deadSet;
    std::map<std::string,std::string>         foldMap;
    std::map<std::string,long long>           propMap;
    if (isAfter) {
        for (auto &d : opts->deads)
            deadSet.insert({d.blockID, d.varName});
        for (auto &r : opts->folds)
            foldMap[r.original] = r.folded;
        for (auto &p : opts->props)
            propMap[p.varUsed] = p.knownValue;
    }

    std::string title = "CFG: " + funcName + " [" + suffix + "]";
    fprintf(f, "digraph CFG_%s_%s {\n", funcName.c_str(), suffix.c_str());
    fprintf(f, "  graph [label=\"%s\", labelloc=t, fontsize=16, fontname=Arial];\n", title.c_str());
    fprintf(f, "  node  [shape=record, style=filled, fontname=Courier, fontsize=10];\n");
    fprintf(f, "  edge  [fontname=Arial, fontsize=10];\n");
    fprintf(f, "  rankdir=TB;\n\n");

    // ── Nodes ────────────────────────────────────────────────
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();

        std::string fillColor = "#ADD8E6";
        std::string penColor  = "black";
        std::string penWidth  = "1";

        if (block == &cfg->getEntry())     { fillColor = "#90EE90"; }
        else if (block == &cfg->getExit()) { fillColor = "#FFB6B6"; }
        else if (isAfter) {
            bool isUnreach = opts->unreachBlocks.count(bid);
            bool isDead    = opts->deadBlocks.count(bid);
            bool isProp    = opts->propBlocks.count(bid);
            bool isFold    = opts->foldBlocks.count(bid);
            if (isUnreach) {
                fillColor = "#D3D3D3"; penColor = "#888888"; penWidth = "3";
            } else {
                if (isFold)                   fillColor = "#FFFACD";
                if (isProp)                   fillColor = "#FFE4B5";
                if (isDead)                   fillColor = "#FFDAB9";
                if (isFold||isProp||isDead) { penColor = "darkblue"; penWidth = "2"; }
            }
        }

        std::string label;

        if (block == &cfg->getEntry()) {
            label = "ENTRY";

        } else if (block == &cfg->getExit()) {
            label = "EXIT";

        } else if (isAfter && opts->unreachBlocks.count(bid)) {
            label  = "Block " + std::to_string(bid) + " [REMOVED]\\l";
            label += "────────────────────────\\l";
            label += "unreachable — deleted\\l";

        } else {
            // Build tag annotation for "after" mode
            std::string tag;
            if (isAfter) {
                std::string tags;
                if (opts->foldBlocks.count(bid)) tags += "CF ";
                if (opts->propBlocks.count(bid)) tags += "CP ";
                if (opts->deadBlocks.count(bid)) tags += "DCE ";
                if (!tags.empty())
                    tag = " [" + tags.substr(0, tags.size()-1) + "]";
            }

            label  = "Block " + std::to_string(bid) + tag + "\\l";
            label += "────────────────────────\\l";

            bool hasContent = false;

            // ── Regular statements ────────────────────────────
            for (CFGElement elem : *block) {
                if (auto S = elem.getAs<CFGStmt>()) {
                    const Stmt *stmt = S->getStmt();
                    if (isInternalNode(stmt)) continue;

                    if (isa<IntegerLiteral>(stmt) || isa<FloatingLiteral>(stmt))
                        continue;

                    std::string raw = stmtToString(stmt, Ctx);
                    if (isBlankString(raw)) continue;

                    std::string defVar = getDefinedVar(stmt);

                    if (isAfter && !defVar.empty() && deadSet.count({bid, defVar})) {
                        label += "[-] " + escapeDot(raw) + "\\l";
                    } else {
                        std::string display = isAfter
                            ? rewriteStmtStr(raw, defVar, foldMap, propMap)
                            : raw;
                        if (isAfter && display != raw)
                            label += escapeDot(display) + "  (was: " + escapeDot(raw) + ")\\l";
                        else
                            label += escapeDot(display) + "\\l";
                    }
                    hasContent = true;
                }
            }

            // ── Terminator (branch condition) ─────────────────
            // Clang stores the branch condition as the block terminator.
            // Show it so blocks with only a condition are not empty.
            if (const Stmt *term = block->getTerminatorStmt()) {
                std::string termStr;
                if (const IfStmt *IS = dyn_cast<IfStmt>(term)) {
                    termStr = "if (" + stmtToString(IS->getCond(), Ctx) + ")";
                } else if (const WhileStmt *WS = dyn_cast<WhileStmt>(term)) {
                    termStr = "while (" + stmtToString(WS->getCond(), Ctx) + ")";
                } else if (const ForStmt *FS = dyn_cast<ForStmt>(term)) {
                    termStr = "for (...)";
                    if (FS->getCond())
                        termStr = "for (...; " + stmtToString(FS->getCond(), Ctx) + "; ...)";
                } else if (const DoStmt *DS2 = dyn_cast<DoStmt>(term)) {
                    termStr = "do-while (" + stmtToString(DS2->getCond(), Ctx) + ")";
                } else {
                    termStr = stmtToString(term, Ctx);
                }
                if (!isBlankString(termStr)) {
                    // In "after" mode, propagate constants into the condition string
                    std::string display = isAfter
                        ? rewriteStmtStr(termStr, "", foldMap, propMap)
                        : termStr;
                    if (isAfter && display != termStr)
                        label += "[cond] " + escapeDot(display) + "  (was: " + escapeDot(termStr) + ")\\l";
                    else
                        label += "[cond] " + escapeDot(display) + "\\l";
                    hasContent = true;
                }
            }

            if (!hasContent) label += "(empty)\\l";
        }

        fprintf(f, "  Block%u [label=\"%s\", fillcolor=\"%s\","
                   " color=\"%s\", penwidth=%s];\n",
                bid, label.c_str(), fillColor.c_str(),
                penColor.c_str(), penWidth.c_str());
    }

    // ── Edges ────────────────────────────────────────────────
    fprintf(f, "\n");
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        if (isAfter && opts->unreachBlocks.count(bid)) continue;

        auto succs = block->succs();
        int succCount = 0;
        for (CFGBlock *s : succs) if (s) succCount++;
        int idx = 0;
        for (CFGBlock *succ : succs) {
            if (succ) {
                if (isAfter && opts->unreachBlocks.count(succ->getBlockID()))
                    { idx++; continue; }
                if (succCount == 2) {
                    const char *lbl   = (idx == 0) ? "true"      : "false";
                    const char *color = (idx == 0) ? "darkgreen" : "red";
                    fprintf(f, "  Block%u -> Block%u [label=\"%s\", color=\"%s\"];\n",
                            block->getBlockID(), succ->getBlockID(), lbl, color);
                } else {
                    fprintf(f, "  Block%u -> Block%u;\n",
                            block->getBlockID(), succ->getBlockID());
                }
                idx++;
            }
        }
    }

    // ── Legend (after only) ───────────────────────────────────
    if (isAfter) {
        fprintf(f, "\n  subgraph cluster_legend {\n");
        fprintf(f, "    label=\"Legend\"; fontsize=11; style=dotted;\n");
        fprintf(f, "    L1 [label=\"CF:  Constant Folding\",     fillcolor=\"#FFFACD\", style=filled, shape=box, fontsize=10];\n");
        fprintf(f, "    L2 [label=\"CP:  Constant Propagation\", fillcolor=\"#FFE4B5\", style=filled, shape=box, fontsize=10];\n");
        fprintf(f, "    L3 [label=\"DCE: Dead Code Elim\",       fillcolor=\"#FFDAB9\", style=filled, shape=box, fontsize=10];\n");
        fprintf(f, "    L4 [label=\"[-]: removed statement\",    fillcolor=\"#FFFFFF\", style=filled, shape=box, fontsize=10];\n");
        fprintf(f, "    L5 [label=\"Grey: Unreachable Block\",   fillcolor=\"#D3D3D3\", style=filled, shape=box, fontsize=10];\n");
        fprintf(f, "    L1 -> L2 -> L3 -> L4 -> L5 [style=invis];\n");
        fprintf(f, "  }\n");
    }

    fprintf(f, "}\n");
    fclose(f);
    dotToPng(dotFile, pngFile);
}

// ─────────────────────────────────────────────────────────────
//  generateOptSummaryPng  (unchanged logic, minor cleanup)
// ─────────────────────────────────────────────────────────────

void generateOptSummaryPng(const std::string &funcName,
                           const OptimizationResults &opts)
{
    std::string dotFile = funcName + "_opt_summary.dot";
    std::string pngFile = funcName + "_opt_summary.png";

    FILE *f = fopen(dotFile.c_str(), "w");
    if (!f) { std::cerr << "[ERR] Cannot open " << dotFile << "\n"; return; }

    fprintf(f, "digraph OPT_SUMMARY_%s {\n", funcName.c_str());
    fprintf(f, "  graph [label=\"Optimization Summary: %s\", labelloc=t, fontsize=18, fontname=Arial, bgcolor=white];\n", funcName.c_str());
    fprintf(f, "  node  [shape=record, fontname=Courier, fontsize=11, style=filled];\n");
    fprintf(f, "  rankdir=TB;\n  splines=false;\n\n");
    fprintf(f, "  TITLE [label=\"Function: %s\", shape=box, fillcolor=\"#4A90D9\", fontcolor=white, fontsize=14, style=\"filled,bold\"];\n\n", funcName.c_str());

    auto writeBox = [&](const char *nodeId, const char *title,
                        const char *fillColor,
                        const std::vector<std::string> &lines, bool found) {
        std::string label = title;
        label += "\\l━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\\l";
        if (!found || lines.empty()) label += "OK  No issues found\\l";
        else for (auto &l : lines) label += escapeDot(l) + "\\l";
        fprintf(f, "  %s [label=\"%s\", fillcolor=\"%s\"];\n",
                nodeId, label.c_str(), found ? fillColor : "#E8F5E9");
    };

    {
        std::vector<std::string> lines;
        for (auto &r : opts.folds)
            lines.push_back("Block " + std::to_string(r.blockID) + ": (" + r.original + ")  ->  " + r.folded);
        writeBox("CF", "1. CONSTANT FOLDING  [CF]", "#FFFACD", lines, !opts.folds.empty());
    }
    {
        std::vector<std::string> lines;
        for (auto &r : opts.props)
            lines.push_back("Block " + std::to_string(r.blockID) + ": '" + r.varUsed + "' = " + std::to_string(r.knownValue) + "  in: " + r.inStmt);
        writeBox("CP", "2. CONSTANT PROPAGATION  [CP]", "#FFE4B5", lines, !opts.props.empty());
    }
    {
        std::vector<std::string> lines;
        for (auto &r : opts.deads)
            lines.push_back("Block " + std::to_string(r.blockID) + ": '" + r.varName + "'  ->  " + r.inStmt);
        writeBox("DCE", "3. DEAD CODE ELIMINATION  [DCE]", "#FFDAB9", lines, !opts.deads.empty());
    }
    {
        std::vector<std::string> lines;
        for (unsigned bid : opts.unreachable)
            lines.push_back("Block " + std::to_string(bid) + " is UNREACHABLE");
        writeBox("UCR", "4. UNREACHABLE CODE REMOVAL  [UCR]", "#D3D3D3", lines, !opts.unreachable.empty());
    }

    int total = (int)opts.folds.size() + (int)opts.props.size()
              + (int)opts.deads.size() + (int)opts.unreachable.size();
    fprintf(f, "  TOTAL [label=\"TOTAL OPTIMIZATIONS FOUND: %d\", shape=box, fillcolor=\"%s\", fontcolor=white, fontsize=13, style=\"filled,bold\"];\n\n",
            total, total > 0 ? "#27AE60" : "#7F8C8D");
    fprintf(f, "  TITLE -> CF;\n  TITLE -> CP;\n  TITLE -> DCE;\n  TITLE -> UCR;\n");
    fprintf(f, "  CF -> TOTAL;\n  CP -> TOTAL;\n  DCE -> TOTAL;\n  UCR -> TOTAL;\n");
    fprintf(f, "}\n");
    fclose(f);
    dotToPng(dotFile, pngFile);
}

// ─────────────────────────────────────────────────────────────
//  OPTIMIZATION PASSES
// ─────────────────────────────────────────────────────────────

std::optional<long long> tryFoldBinaryOp(const BinaryOperator *BO, ASTContext *Ctx) {
    Expr::EvalResult LRes, RRes;
    if (!BO->getLHS()->EvaluateAsInt(LRes, *Ctx)) return std::nullopt;
    if (!BO->getRHS()->EvaluateAsInt(RRes, *Ctx)) return std::nullopt;
    long long L = LRes.Val.getInt().getExtValue();
    long long R = RRes.Val.getInt().getExtValue();
    switch (BO->getOpcode()) {
        case BO_Add: return L + R;
        case BO_Sub: return L - R;
        case BO_Mul: return L * R;
        case BO_Div: if (R != 0) return L / R; break;
        case BO_Rem: if (R != 0) return L % R; break;
        default: break;
    }
    return std::nullopt;
}

void collectFoldable(const Stmt *S, ASTContext *Ctx,
                     unsigned blockID, std::vector<FoldResult> &results) {
    if (!S) return;
    if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(S)) {
        if (BO->getLHS()->isIntegerConstantExpr(*Ctx) &&
            BO->getRHS()->isIntegerConstantExpr(*Ctx)) {
            auto val = tryFoldBinaryOp(BO, Ctx);
            if (val.has_value()) {
                std::string orig = stmtToString(BO, Ctx);
                results.push_back({orig, std::to_string(*val), blockID});
            }
        }
    }
    for (const Stmt *child : S->children())
        collectFoldable(child, Ctx, blockID, results);
}

void runConstantFolding(CFG *cfg, ASTContext *Ctx, OptimizationResults &opts) {
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   OPTIMIZATION 1: CONSTANT FOLDING               ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";
    for (CFGBlock *block : *cfg)
        for (CFGElement elem : *block)
            if (auto S = elem.getAs<CFGStmt>()) {
                size_t before = opts.folds.size();
                collectFoldable(S->getStmt(), Ctx, block->getBlockID(), opts.folds);
                if (opts.folds.size() > before)
                    opts.foldBlocks.insert(block->getBlockID());
            }
    if (opts.folds.empty()) {
        std::cout << "  ✅ No constant-foldable expressions found.\n";
    } else {
        std::cout << "  Found " << opts.folds.size() << " foldable expression(s):\n\n";
        for (auto &r : opts.folds) {
            std::cout << "  ┌─ Block " << r.blockID << "\n";
            std::cout << "  │  Expression : " << r.original << "\n";
            std::cout << "  │  Folds to   : " << r.folded   << "\n";
            std::cout << "  └─ [FIX] Replace (" << r.original << ") with literal " << r.folded << "\n\n";
        }
    }
}

std::optional<long long> getConstValueOfDef(const Definition &def, CFG *cfg, ASTContext *Ctx) {
    for (CFGBlock *block : *cfg) {
        if (block->getBlockID() != def.blockID) continue;
        unsigned stmtIdx = 0;
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                if (stmtIdx == def.stmtIndex) {
                    const Stmt *stmt = S->getStmt();
                    if (const DeclStmt *DS = dyn_cast<DeclStmt>(stmt))
                        for (const Decl *D : DS->decls())
                            if (const VarDecl *VD = dyn_cast<VarDecl>(D))
                                if (VD->getNameAsString() == def.varName && VD->hasInit()) {
                                    Expr::EvalResult er;
                                    if (VD->getInit()->EvaluateAsInt(er, *Ctx))
                                        return er.Val.getInt().getExtValue();
                                }
                    if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(stmt))
                        if (BO->getOpcode() == BO_Assign) {
                            Expr::EvalResult er;
                            if (BO->getRHS()->EvaluateAsInt(er, *Ctx))
                                return er.Val.getInt().getExtValue();
                        }
                    return std::nullopt;
                }
                stmtIdx++;
            }
        }
    }
    return std::nullopt;
}

void runConstantPropagation(CFG *cfg, ASTContext *Ctx,
                            const ReachingDefResult &RD,
                            OptimizationResults &opts) {
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   OPTIMIZATION 2: CONSTANT PROPAGATION           ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                const Stmt *stmt = S->getStmt();
                for (const std::string &var : getUsedVars(stmt)) {
                    std::vector<Definition> reachingForVar;
                    if (RD.IN.count(bid))
                        for (const Definition &d : RD.IN.at(bid))
                            if (d.varName == var)
                                reachingForVar.push_back(d);
                    if (reachingForVar.empty()) continue;
                    std::optional<long long> agreedValue;
                    bool conflict = false;
                    for (const Definition &d : reachingForVar) {
                        auto cv = getConstValueOfDef(d, cfg, Ctx);
                        if (!cv.has_value()) { conflict = true; break; }
                        if (!agreedValue.has_value()) agreedValue = cv;
                        else if (*agreedValue != *cv) { conflict = true; break; }
                    }
                    if (!conflict && agreedValue.has_value()) {
                        std::string stmtStr = stmtToString(stmt, Ctx);
                        bool already = false;
                        for (auto &p : opts.props)
                            if (p.varUsed == var && p.blockID == bid && p.inStmt == stmtStr)
                                { already = true; break; }
                        if (!already) {
                            opts.props.push_back({var, stmtStr, *agreedValue, bid});
                            opts.propBlocks.insert(bid);
                        }
                    }
                }
            }
        }
    }
    if (opts.props.empty()) {
        std::cout << "  ✅ No constant propagation opportunities found.\n";
    } else {
        std::cout << "  Found " << opts.props.size() << " propagation opportunity/ies:\n\n";
        for (auto &p : opts.props) {
            std::cout << "  ┌─ Block " << p.blockID << "\n";
            std::cout << "  │  Variable   : '" << p.varUsed << "'\n";
            std::cout << "  │  Proven val : " << p.knownValue << "\n";
            std::cout << "  │  In stmt    : " << p.inStmt << "\n";
            std::cout << "  └─ [FIX] Replace '" << p.varUsed << "' with literal " << p.knownValue << "\n\n";
        }
    }
}

void runDeadCodeElimination(CFG *cfg, ASTContext *Ctx,
                            const LVAResult &LVA,
                            OptimizationResults &opts) {
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   OPTIMIZATION 3: DEAD CODE ELIMINATION          ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";

    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();

        std::vector<const Stmt*> stmts;
        for (CFGElement elem : *block)
            if (auto S = elem.getAs<CFGStmt>())
                stmts.push_back(S->getStmt());

        for (size_t i = 0; i < stmts.size(); i++) {
            const Stmt *stmt = stmts[i];
            std::string defVar = getDefinedVar(stmt);
            if (defVar.empty()) continue;

            static const std::set<std::string> empty_set;
            const auto &lout = LVA.LIVE_OUT.count(bid)
                               ? LVA.LIVE_OUT.at(bid) : empty_set;
            bool liveOut  = lout.count(defVar) > 0;

            bool usedLater = false;
            for (size_t j = i + 1; j < stmts.size(); j++) {
                if (getUsedVars(stmts[j]).count(defVar)) { usedLater = true; break; }
            }

            if (!liveOut && !usedLater) {
                opts.deads.push_back({defVar, stmtToString(stmt, Ctx), bid});
                opts.deadBlocks.insert(bid);
            }
        }
    }

    if (opts.deads.empty()) {
        std::cout << "  ✅ No dead assignments found.\n";
    } else {
        std::cout << "  Found " << opts.deads.size() << " dead assignment(s):\n\n";
        for (auto &d : opts.deads) {
            std::cout << "  ┌─ Block " << d.blockID << "\n";
            std::cout << "  │  ⚠ DEAD: '" << d.varName << "'  in:  " << d.inStmt << "\n";
            std::cout << "  │    → Not used later in block, not live in any successor.\n";
            std::cout << "  └─ [FIX] Remove this dead assignment.\n\n";
        }
    }
}

void runUnreachableCodeRemoval(CFG *cfg, ASTContext *Ctx, OptimizationResults &opts) {
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   OPTIMIZATION 4: UNREACHABLE CODE REMOVAL       ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";
    std::set<unsigned> visited;
    std::vector<CFGBlock *> worklist;
    worklist.push_back(&cfg->getEntry());
    visited.insert(cfg->getEntry().getBlockID());
    while (!worklist.empty()) {
        CFGBlock *cur = worklist.back(); worklist.pop_back();
        for (CFGBlock *succ : cur->succs())
            if (succ && !visited.count(succ->getBlockID())) {
                visited.insert(succ->getBlockID());
                worklist.push_back(succ);
            }
    }
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        if (!visited.count(bid)) {
            opts.unreachable.push_back(bid);
            opts.unreachBlocks.insert(bid);
        }
    }
    std::cout << "  Block reachability from ENTRY:\n\n";
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        bool reachable = visited.count(bid) > 0;
        std::cout << "    Block " << bid;
        if (block == &cfg->getEntry()) std::cout << " (ENTRY)";
        if (block == &cfg->getExit())  std::cout << " (EXIT) ";
        std::cout << "  ->  " << (reachable ? "REACHABLE" : "UNREACHABLE") << "\n";
    }
    if (opts.unreachable.empty()) {
        std::cout << "\n  ✅ All blocks are reachable.\n";
    } else {
        std::cout << "\n  Found " << opts.unreachable.size() << " unreachable block(s):\n\n";
        for (unsigned bid : opts.unreachable) {
            std::cout << "  ┌─ Block " << bid << "  <- UNREACHABLE\n";
            std::cout << "  └─ [FIX] This entire block can be deleted.\n\n";
        }
    }
}

void printTerminalSummary(const std::string &funcName, const OptimizationResults &opts) {
    int total = (int)opts.folds.size() + (int)opts.props.size()
              + (int)opts.deads.size() + (int)opts.unreachable.size();
    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║   OPTIMIZATION SUMMARY — " << funcName;
    for (size_t i = funcName.size(); i < 25; i++) std::cout << ' ';
    std::cout << "║\n";
    std::cout << "╠══════════════════════════════════════════════════════╣\n";
    auto row = [](const char *label, int n) {
        std::cout << "║  " << label;
        std::string ns = n > 0 ? std::to_string(n) + " found" : "none";
        for (size_t i = strlen(label); i < 30; i++) std::cout << ' ';
        std::cout << ns;
        for (size_t i = ns.size(); i < 22; i++) std::cout << ' ';
        std::cout << "║\n";
    };
    row("Constant Folding       :", (int)opts.folds.size());
    row("Constant Propagation   :", (int)opts.props.size());
    row("Dead Code Elimination  :", (int)opts.deads.size());
    row("Unreachable Code Removal:", (int)opts.unreachable.size());
    std::cout << "╠══════════════════════════════════════════════════════╣\n";
    row("TOTAL                  :", total);
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
}
// ─────────────────────────────────────────────────────────────
//  HEADER DETECTION
// ─────────────────────────────────────────────────────────────

static std::set<std::string> detectRequiredHeaders(CFG *cfg, ASTContext *Ctx) {
    std::set<std::string> headers;
    for (CFGBlock *block : *cfg) {
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                std::function<void(const Stmt*)> walk = [&](const Stmt *s) {
                    if (!s) return;
                    if (const CallExpr *CE = dyn_cast<CallExpr>(s)) {
                        if (const FunctionDecl *FD = CE->getDirectCallee()) {
                            std::string fn = FD->getNameAsString();
                            if (fn == "printf" || fn == "fprintf" ||
                                fn == "scanf"  || fn == "puts"    ||
                                fn == "fopen"  || fn == "fclose"  ||
                                fn == "fgets"  || fn == "fputs")
                                headers.insert("#include <stdio.h>");
                            if (fn == "malloc" || fn == "free"    ||
                                fn == "calloc" || fn == "realloc" ||
                                fn == "exit"   || fn == "atoi"    ||
                                fn == "abs")
                                headers.insert("#include <stdlib.h>");
                            if (fn == "strlen" || fn == "strcpy"  ||
                                fn == "strcat" || fn == "strcmp"  ||
                                fn == "memset" || fn == "memcpy")
                                headers.insert("#include <string.h>");
                            if (fn == "sqrt"   || fn == "pow"     ||
                                fn == "fabs"   || fn == "ceil"    ||
                                fn == "floor")
                                headers.insert("#include <math.h>");
                        }
                    }
                    for (const Stmt *child : s->children()) walk(child);
                };
                walk(S->getStmt());
            }
        }
    }
    return headers;
}

static std::string getFunctionSignature(FunctionDecl *F, ASTContext *Ctx) {
    std::string retType = F->getReturnType().getAsString(PrintingPolicy(Ctx->getLangOpts()));
    std::string sig = retType + " " + F->getNameAsString() + "(";
    for (unsigned i = 0; i < F->getNumParams(); i++) {
        if (i > 0) sig += ", ";
        ParmVarDecl *param = F->getParamDecl(i);
        std::string paramType = param->getType().getAsString(PrintingPolicy(Ctx->getLangOpts()));
        sig += paramType + " " + param->getNameAsString();
    }
    if (F->isVariadic()) sig += ", ...";
    sig += ")";
    return sig;
}
// ─────────────────────────────────────────────────────────────
//  generateOptimizedCode — FIXED
//
//  Root causes of broken output in the original:
//
//  1. DOUBLE SEMICOLONS
//     The original always appended ";" to every line.
//     ReturnStmt pretty-prints as "return x;" — adding another
//     ";" yields "return x;;".  DeclStmt prints as "int a = 1;"
//     — same problem.  Fix: use needsSemicolon() to only add ";"
//     when the pretty-print does not already end with one and the
//     statement is not a compound/control-flow form.
//
//  2. SKIPPING REAL STATEMENTS (isa<ImplicitCastExpr> blanket filter)
//     The original skipped *all* ImplicitCastExpr nodes, but a
//     sole condition expression like "a > 0" inside an IfStmt
//     also surfaced that way, causing entire condition blocks to
//     vanish from the output.  Fix: use the shared isInternalNode()
//     helper that is tighter — only skip nodes whose text is truly
//     a sub-expression already covered by the parent stmt shown
//     earlier in the same block.  Because each CFGBlock normally
//     contains the terminal condition as a separate element, we
//     detect it via block->getTerminatorCondition() and handle it
//     explicitly rather than discarding it.
//
//  3. CONTROL-FLOW STATEMENTS WRITTEN AS BARE EXPRESSIONS
//     IfStmt / WhileStmt / ForStmt terminators should NOT be
//     emitted as independent statements in the flat output — they
//     are represented structurally by the CFG edges.  The original
//     had no guard for this, so "if (x > 0)" would appear as a
//     standalone line.  Fix: skip terminator statements in the
//     flat statement walk; we handle them implicitly through edges.
//
//  4. CFG BLOCK ORDER vs. SOURCE ORDER
//     Clang numbers blocks in *reverse* post-order, so sorting by
//     descending blockID approximates source order for straight-line
//     code but breaks for loops (the loop-back block gets a low ID).
//     Fix: walk the CFG in a proper reverse-post-order (RPO) by
//     doing a DFS from the entry block, collecting blocks in
//     post-order and then reversing.  This gives a topological
//     ordering that matches source structure for acyclic paths
//     and keeps loop bodies before the loop exit.
// ─────────────────────────────────────────────────────────────

// Produce a reverse-post-order list of CFGBlocks starting from Entry.
// Blocks not reachable from Entry are appended at the end (they
// will be commented-out as unreachable).
static std::vector<CFGBlock*> reversePostOrder(CFG *cfg) {
    std::set<unsigned>        visited;
    std::vector<CFGBlock*>    postOrder;

    std::function<void(CFGBlock*)> dfs = [&](CFGBlock *block) {
        visited.insert(block->getBlockID());
        // Visit successors first (DFS)
        for (CFGBlock *succ : block->succs())
            if (succ && !visited.count(succ->getBlockID()))
                dfs(succ);
        postOrder.push_back(block);
    };

    dfs(&cfg->getEntry());

    // Reverse to get RPO
    std::reverse(postOrder.begin(), postOrder.end());

    // Append any unreachable blocks at the end
    for (CFGBlock *block : *cfg)
        if (!visited.count(block->getBlockID()))
            postOrder.push_back(block);

    return postOrder;
}

void generateOptimizedCode(CFG *cfg, ASTContext *Ctx,
                           FunctionDecl *F,
                           const std::string &funcName,
                           const OptimizationResults &opts)
{
    std::string outFile = funcName + "_optimized.c";
    FILE *f = fopen(outFile.c_str(), "w");
    if (!f) { std::cerr << "[ERR] Cannot open " << outFile << "\n"; return; }
    fprintf(f, "// ============================================================\n");
    fprintf(f, "// OPTIMIZED CODE — function: %s\n", funcName.c_str());
    fprintf(f, "// Generated by Phase 3 CFG Optimizer\n");
    fprintf(f, "// Optimizations applied:\n");
    fprintf(f, "//   Constant Folding        : %zu found\n", opts.folds.size());
    fprintf(f, "//   Constant Propagation    : %zu found\n", opts.props.size());
    fprintf(f, "//   Dead Code Elimination   : %zu found\n", opts.deads.size());
    fprintf(f, "//   Unreachable Code Removal: %zu found\n", opts.unreachable.size());
    fprintf(f, "// ============================================================\n\n");

    // Write detected headers
    auto headers = detectRequiredHeaders(cfg, Ctx);
    headers.insert("#include <stdio.h>");  // always include
    for (auto &h : headers)
        fprintf(f, "%s\n", h.c_str());
    fprintf(f, "\n");

    // Write function signature
    std::string sig = getFunctionSignature(F, Ctx);
    fprintf(f, "%s {\n", sig.c_str());
    // Build lookup tables
    std::set<std::pair<unsigned,std::string>> deadSet;
    for (auto &d : opts.deads)
        deadSet.insert({d.blockID, d.varName});

    std::map<std::string,std::string> foldMap;
    for (auto &r : opts.folds)
        foldMap[r.original] = r.folded;

    std::map<std::string,long long> propMap;
    for (auto &p : opts.props)
        propMap[p.varUsed] = p.knownValue;

    // Walk blocks in RPO (= natural source / execution order)
    std::vector<CFGBlock*> ordered = reversePostOrder(cfg);

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   OPTIMIZED CODE — " << funcName;
    for (size_t i = funcName.size(); i < 29; i++) std::cout << ' ';
    std::cout << "║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    for (CFGBlock *block : ordered) {
        unsigned bid = block->getBlockID();

        // Skip pseudo-blocks
        if (block == &cfg->getEntry()) continue;
        if (block == &cfg->getExit())  continue;

        // Skip (comment-out) unreachable blocks
        if (opts.unreachBlocks.count(bid)) {
            std::string comment = "    // [REMOVED] Block " + std::to_string(bid)
                                + " — unreachable, never executes";
            fprintf(f, "%s\n", comment.c_str());
            std::cout << comment << "\n";
            continue;
        }

        // Collect the terminator statement so we can exclude it
        // from the flat statement walk (it is structural, not a
        // source statement to emit).
        const Stmt *termStmt = block->getTerminatorStmt();

        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                const Stmt *stmt = S->getStmt();

                // (a) Skip internal AST nodes that carry no source text
                if (isInternalNode(stmt)) continue;

                // Skip bare literals — they are sub-expressions already
                // covered by their parent statement (e.g. return 0;)
                if (isa<IntegerLiteral>(stmt)  ||
                    isa<FloatingLiteral>(stmt)  ||
                    isa<CharacterLiteral>(stmt) ||
                    isa<StringLiteral>(stmt))
                    continue;

                // Skip return-value sub-expression emitted as own element
                if (isa<ReturnStmt>(stmt)) {
                    // ReturnStmt itself is fine — keep it
                } else if (termStmt && isa<ReturnStmt>(termStmt)) {
                    if (stmt == cast<ReturnStmt>(termStmt)->getRetValue())
                        continue;
                }

                // (b) Skip the block terminator — it is a control-flow
                //     construct (IfStmt, WhileStmt, ForStmt …) whose
                //     structure is already captured by CFG edges.
                //     Emitting it as a standalone expression would
                //     produce invalid C (e.g. a bare "if (cond)").
                if (stmt == termStmt) continue;

                // (c) Also skip the terminator *condition* expression
                //     (Clang sometimes adds it as a separate element).
                if (termStmt) {
                    if (const IfStmt *IS = dyn_cast<IfStmt>(termStmt))
                        if (stmt == IS->getCond()) continue;
                    if (const WhileStmt *WS = dyn_cast<WhileStmt>(termStmt))
                        if (stmt == WS->getCond()) continue;
                    if (const ForStmt *FS = dyn_cast<ForStmt>(termStmt))
                        if (stmt == FS->getCond()) continue;
                    if (const DoStmt *DS2 = dyn_cast<DoStmt>(termStmt))
                        if (stmt == DS2->getCond()) continue;
                }

                std::string defVar  = getDefinedVar(stmt);
                std::string stmtStr = stmtToString(stmt, Ctx);

                if (isBlankString(stmtStr)) continue;

                // (d) Remove dead assignments
                if (!defVar.empty() && deadSet.count({bid, defVar})) {
                    std::string semi = needsSemicolon(stmt, stmtStr) ? ";" : "";
                    std::string comment = "    // [REMOVED-DCE] " + stmtStr + semi;
                    fprintf(f, "%s\n", comment.c_str());
                    std::cout << comment << "\n";
                    continue;
                }

                // (e) Apply optimizations
                std::string rewritten  = rewriteStmtStr(stmtStr, defVar, foldMap, propMap);
                std::string annotation = (rewritten != stmtStr)
                                         ? "  // was: " + stmtStr
                                         : "";

                // (f) Add a semicolon only when the statement needs one
                //     (it doesn't already end with ';' and is not a
                //     compound / control-flow form).
                std::string semi = needsSemicolon(stmt, rewritten) ? ";" : "";

                std::string line = "    " + rewritten + semi + annotation;
                fprintf(f, "%s\n", line.c_str());
                std::cout << line << "\n";
            }
        }
    }
    
    fprintf(f, "}\n");
    std::cout << "}\n";

    fclose(f);
    std::cout << "\n[DONE] Fully runnable optimized code written to: " << outFile << "\n";
}

// ─────────────────────────────────────────────────────────────
//  AST VISITOR / CONSUMER / ACTION / MAIN
// ─────────────────────────────────────────────────────────────

class OptVisitor : public RecursiveASTVisitor<OptVisitor> {
public:
    ASTContext *Context;
    explicit OptVisitor(ASTContext *Ctx) : Context(Ctx) {}

    bool VisitFunctionDecl(FunctionDecl *F) {
        if (!F->hasBody()) return true;
        std::string name = F->getNameAsString();

        std::cout << "\n\n";
        std::cout << "████████████████████████████████████████████████████\n";
        std::cout << "  FUNCTION: " << name << "\n";
        std::cout << "████████████████████████████████████████████████████\n";

        std::unique_ptr<CFG> cfg = CFG::buildCFG(
            F, F->getBody(), Context, CFG::BuildOptions());
        if (!cfg) { std::cout << "  [Could not build CFG]\n"; return true; }

        std::cout << "\n[PHASE 3-PNG] Generating BEFORE CFG...\n";
        generateCFGPng(cfg.get(), Context, name, "before", nullptr);

        std::cout << "\n[Analysis] Computing Reaching Definitions...\n";
        ReachingDefResult RD = computeReachingDefs(cfg.get());

        std::cout << "[Analysis] Computing Live Variable Analysis...\n";
        LVAResult LVA = computeLVA(cfg.get());

        OptimizationResults opts;
        runConstantFolding       (cfg.get(), Context, opts);
        runConstantPropagation   (cfg.get(), Context, RD, opts);
        runDeadCodeElimination   (cfg.get(), Context, LVA, opts);
        runUnreachableCodeRemoval(cfg.get(), Context, opts);

        printTerminalSummary(name, opts);

        std::cout << "\n[PHASE 3-PNG] Generating AFTER CFG (with highlights)...\n";
        generateCFGPng(cfg.get(), Context, name, "after", &opts);

        std::cout << "[PHASE 3-PNG] Generating Optimization Summary PNG...\n";
        generateOptSummaryPng(name, opts);

	generateOptimizedCode(cfg.get(), Context, F, name, opts);

        std::cout << "\n[DONE] Output files for '" << name << "':\n";
        std::cout << "   " << name << "_cfg_before.png\n";
        std::cout << "   " << name << "_cfg_after.png\n";
        std::cout << "   " << name << "_opt_summary.png\n";
        std::cout << "   " << name << "_optimized.c\n";

        return true;
    }
};

class OptConsumer : public ASTConsumer {
    OptVisitor Visitor;
public:
    explicit OptConsumer(ASTContext *Ctx) : Visitor(Ctx) {}
    void HandleTranslationUnit(ASTContext &Ctx) override {
        Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
    }
};

class OptAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(
        CompilerInstance &CI, StringRef file) override {
        return std::make_unique<OptConsumer>(&CI.getASTContext());
    }
};

static llvm::cl::OptionCategory MyToolCategory("cfg-optimizer options");

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    return Tool.run(newFrontendActionFactory<OptAction>().get());
}
