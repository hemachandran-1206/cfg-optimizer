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
#include <string>
#include <fstream>

using namespace clang;
using namespace clang::tooling;

std::string escapeDot(const std::string &s) {
    std::string out;
    for (char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '{')  out += "\\{";
        else if (c == '}')  out += "\\}";
        else if (c == '<')  out += "\\<";
        else if (c == '>')  out += "\\>";
        else out += c;
    }
    if (out.length() > 35) out = out.substr(0, 35) + "...";
    return out;
}

void generateCFGImage(CFG *cfg, ASTContext *Context, const std::string &funcName) {
    std::string dotFile = funcName + "_cfg.dot";
    FILE *f = fopen(dotFile.c_str(), "w");
    if (!f) return;

    fprintf(f, "digraph CFG_%s {\n", funcName.c_str());
    fprintf(f, "  graph [label=\"CFG: %s\", labelloc=t, fontsize=16, fontname=Arial];\n", funcName.c_str());
    fprintf(f, "  node [shape=box, style=filled, fontname=Courier, fontsize=11];\n");
    fprintf(f, "  edge [fontname=Arial, fontsize=10];\n");
    fprintf(f, "  rankdir=TB;\n\n");

    for (CFGBlock *block : *cfg) {
        std::string label;
        std::string color;

        if (block == &cfg->getEntry()) {
            label = "ENTRY";
            color = "#90EE90";
        } else if (block == &cfg->getExit()) {
            label = "EXIT";
            color = "#FFB6B6";
        } else {
            label = "Block " + std::to_string(block->getBlockID()) + "\\n";
            label += "━━━━━━━━━━━━━━━━\\n";
            bool hasStmts = false;
            for (CFGElement elem : *block) {
                if (auto S = elem.getAs<CFGStmt>()) {
                    std::string stmtStr;
                    llvm::raw_string_ostream rso(stmtStr);
                    S->getStmt()->printPretty(rso, nullptr,
                        PrintingPolicy(Context->getLangOpts()));
                    rso.flush();
                    label += escapeDot(stmtStr) + "\\n";
                    hasStmts = true;
                }
            }
            if (!hasStmts) label += "(empty)\\n";
            color = "#ADD8E6";
        }

        fprintf(f, "  Block%d [label=\"%s\", fillcolor=\"%s\"];\n",
                block->getBlockID(), label.c_str(), color.c_str());
    }

    fprintf(f, "\n");
    for (CFGBlock *block : *cfg) {
        auto succs = block->succs();
        int succCount = 0;
        for (CFGBlock *s : succs) if (s) succCount++;
        int idx = 0;
        for (CFGBlock *succ : succs) {
            if (succ) {
                if (succCount == 2) {
                    std::string lbl = (idx == 0) ? "true" : "false";
                    fprintf(f, "  Block%d -> Block%d [label=\"%s\", color=\"%s\"];\n",
                            block->getBlockID(), succ->getBlockID(),
                            lbl.c_str(), idx == 0 ? "darkgreen" : "red");
                } else {
                    fprintf(f, "  Block%d -> Block%d;\n",
                            block->getBlockID(), succ->getBlockID());
                }
                idx++;
            }
        }
    }

    fprintf(f, "}\n");
    fclose(f);

    std::string pngFile = funcName + "_cfg.png";
    std::string cmd = "dot -Tpng " + dotFile + " -o " + pngFile + " 2>/dev/null";
    system(cmd.c_str());
    std::cout << "[CFG IMAGE] Saved: " << pngFile << "\n";
}

class CFGVisitor : public RecursiveASTVisitor<CFGVisitor> {
public:
    ASTContext *Context;
    explicit CFGVisitor(ASTContext *Ctx) : Context(Ctx) {}

    bool VisitFunctionDecl(FunctionDecl *F) {
        if (!F->hasBody()) return true;
        std::string name = F->getNameAsString();
        std::cout << "\n--- Function: " << name << " ---\n";
        std::unique_ptr<CFG> cfg = CFG::buildCFG(
            F, F->getBody(), Context, CFG::BuildOptions());
        if (!cfg) { std::cout << "  [Could not build CFG]\n"; return true; }
        generateCFGImage(cfg.get(), Context, name);
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
