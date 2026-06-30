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

using namespace clang;
using namespace clang::tooling;

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
    if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(S))
        if (BO->getOpcode() == BO_Assign)
            if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(BO->getLHS()))
                used.erase(DRE->getDecl()->getNameAsString());
    return used;
}

void reachingDefinitions(CFG *cfg, ASTContext *Context) {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║   PHASE 2A: REACHING DEFINITIONS         ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    std::vector<Definition> allDefs;
    for (CFGBlock *block : *cfg) {
        unsigned stmtIdx = 0;
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                std::string var = getDefinedVar(S->getStmt());
                if (!var.empty())
                    allDefs.push_back({var, block->getBlockID(), stmtIdx});
                stmtIdx++;
            }
        }
    }

    std::map<unsigned, std::set<Definition>> GEN, KILL;
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        unsigned stmtIdx = 0;
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                std::string var = getDefinedVar(S->getStmt());
                if (!var.empty()) {
                    Definition def = {var, bid, stmtIdx};
                    GEN[bid].insert(def);
                    for (const Definition &d : allDefs)
                        if (d.varName == var && !(d == def))
                            KILL[bid].insert(d);
                }
                stmtIdx++;
            }
        }
    }

    std::map<unsigned, std::set<Definition>> IN, OUT;
    bool changed = true;
    while (changed) {
        changed = false;
        for (CFGBlock *block : *cfg) {
            unsigned bid = block->getBlockID();
            std::set<Definition> newIN;
            for (CFGBlock *pred : block->preds())
                if (pred)
                    for (const Definition &d : OUT[pred->getBlockID()])
                        newIN.insert(d);
            std::set<Definition> newOUT = GEN[bid];
            for (const Definition &d : newIN)
                if (KILL[bid].find(d) == KILL[bid].end())
                    newOUT.insert(d);
            if (newIN != IN[bid] || newOUT != OUT[bid]) {
                IN[bid] = newIN; OUT[bid] = newOUT; changed = true;
            }
        }
    }

    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        std::cout << "\n┌─ Block " << bid;
        if (block == &cfg->getEntry()) std::cout << " (ENTRY)";
        if (block == &cfg->getExit())  std::cout << " (EXIT)";
        std::cout << "\n";

        std::cout << "│  GEN  : ";
        if (GEN[bid].empty()) std::cout << "(none)";
        for (auto &d : GEN[bid])
            std::cout << d.varName << "@B" << d.blockID << "  ";
        std::cout << "\n";

        std::cout << "│  KILL : ";
        if (KILL[bid].empty()) std::cout << "(none)";
        for (auto &d : KILL[bid])
            std::cout << d.varName << "@B" << d.blockID << "  ";
        std::cout << "\n";

        std::cout << "│  IN   : ";
        if (IN[bid].empty()) std::cout << "(none)";
        for (auto &d : IN[bid])
            std::cout << d.varName << "@B" << d.blockID << "  ";
        std::cout << "\n";

        std::cout << "│  OUT  : ";
        if (OUT[bid].empty()) std::cout << "(none)";
        for (auto &d : OUT[bid])
            std::cout << d.varName << "@B" << d.blockID << "  ";
        std::cout << "\n└─────────────────────────\n";
    }

    std::cout << "\n⚠  UNINITIALIZED VARIABLE CHECK:\n";
    bool found = false;
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                if (const DeclStmt *DS = dyn_cast<DeclStmt>(S->getStmt()))
                    for (const Decl *D : DS->decls())
                        if (const VarDecl *VD = dyn_cast<VarDecl>(D))
                            if (!VD->hasInit()) {
                                std::cout << "   [WARNING] '"
                                    << VD->getNameAsString()
                                    << "' declared without init in Block " << bid << "\n";
                                found = true;
                            }
            }
        }
    }
    if (!found) std::cout << "   No uninitialized variables found.\n";
}

void liveVariableAnalysis(CFG *cfg, ASTContext *Context) {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║   PHASE 2B: LIVE VARIABLE ANALYSIS       ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    std::map<unsigned, std::set<std::string>> USE, DEF;
    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        for (CFGElement elem : *block) {
            if (auto S = elem.getAs<CFGStmt>()) {
                const Stmt *stmt = S->getStmt();
                std::string defVar = getDefinedVar(stmt);
                std::set<std::string> usedHere = getUsedVars(stmt);
                for (auto &v : usedHere)
                    if (DEF[bid].find(v) == DEF[bid].end())
                        USE[bid].insert(v);
                if (!defVar.empty())
                    DEF[bid].insert(defVar);
            }
        }
    }

    std::map<unsigned, std::set<std::string>> LIVE_IN, LIVE_OUT;
    bool changed = true;
    while (changed) {
        changed = false;
        for (CFGBlock *block : *cfg) {
            unsigned bid = block->getBlockID();
            std::set<std::string> newOUT;
            for (CFGBlock *succ : block->succs())
                if (succ)
                    for (auto &v : LIVE_IN[succ->getBlockID()])
                        newOUT.insert(v);
            std::set<std::string> newIN = USE[bid];
            for (auto &v : newOUT)
                if (DEF[bid].find(v) == DEF[bid].end())
                    newIN.insert(v);
            if (newIN != LIVE_IN[bid] || newOUT != LIVE_OUT[bid]) {
                LIVE_IN[bid] = newIN; LIVE_OUT[bid] = newOUT; changed = true;
            }
        }
    }

    for (CFGBlock *block : *cfg) {
        unsigned bid = block->getBlockID();
        std::cout << "\n┌─ Block " << bid;
        if (block == &cfg->getEntry()) std::cout << " (ENTRY)";
        if (block == &cfg->getExit())  std::cout << " (EXIT)";
        std::cout << "\n";

        std::cout << "│  USE      : ";
        if (USE[bid].empty()) std::cout << "(none)";
        for (auto &v : USE[bid]) std::cout << v << "  ";
        std::cout << "\n";

        std::cout << "│  DEF      : ";
        if (DEF[bid].empty()) std::cout << "(none)";
        for (auto &v : DEF[bid]) std::cout << v << "  ";
        std::cout << "\n";

        std::cout << "│  LIVE_IN  : ";
        if (LIVE_IN[bid].empty()) std::cout << "(none)";
        for (auto &v : LIVE_IN[bid]) std::cout << v << "  ";
        std::cout << "\n";

        std::cout << "│  LIVE_OUT : ";
        if (LIVE_OUT[bid].empty()) std::cout << "(none)";
        for (auto &v : LIVE_OUT[bid]) std::cout << v << "  ";
        std::cout << "\n";

        for (auto &v : DEF[bid])
            if (LIVE_OUT[bid].find(v) == LIVE_OUT[bid].end())
                std::cout << "│  ⚠ DEAD   : '" << v << "' assigned but never used after this block\n";

        std::cout << "└─────────────────────────\n";
    }
}

class CFGVisitor : public RecursiveASTVisitor<CFGVisitor> {
public:
    ASTContext *Context;
    explicit CFGVisitor(ASTContext *Ctx) : Context(Ctx) {}

    bool VisitFunctionDecl(FunctionDecl *F) {
        if (!F->hasBody()) return true;
        std::string name = F->getNameAsString();
        std::cout << "\n\n════════════════════════════════════════════\n";
        std::cout << "  FUNCTION: " << name << "\n";
        std::cout << "════════════════════════════════════════════\n";
        std::unique_ptr<CFG> cfg = CFG::buildCFG(
            F, F->getBody(), Context, CFG::BuildOptions());
        if (!cfg) { std::cout << "  [Could not build CFG]\n"; return true; }
        reachingDefinitions(cfg.get(), Context);
        liveVariableAnalysis(cfg.get(), Context);
        return true;
    }
};

class CFGConsumer : public ASTConsumer {
    CFGVisitor Visitor;
public:
    explicit CFGConsumer(ASTContext *Ctx) : Visitor(Ctx) {}
    void HandleTranslationUnit(ASTContext &Ctx) override {
        Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
    }
};

class CFGAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(
        CompilerInstance &CI, StringRef file) override {
        return std::make_unique<CFGConsumer>(&CI.getASTContext());
    }
};

static llvm::cl::OptionCategory MyToolCategory("cfg-extractor options");

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    return Tool.run(newFrontendActionFactory<CFGAction>().get());
}
