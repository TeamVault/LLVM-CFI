#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO/SafeDispatchLayoutBuilder.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>
#include <vector>
#include <set>
#include <map>
#include <math.h>
#include <algorithm>
#include <iostream>

// you have to modify the following files for each additional LLVM pass
// 1. IPO.h and IPO.cpp
// 2. LinkAllPasses.h
// 3. InitializePasses.h

using namespace llvm;

namespace {
  /**
   * Pass for updating the annotated instructions with the new indices
   */
  struct SDMoveBasicBlocks : public ModulePass {
    static char ID; // Pass identification, replacement for typeid

    SDMoveBasicBlocks() : ModulePass(ID) {
      sd_print("initializing SDMoveBasicBlocks pass\n");
      initializeSDMoveBasicBlocksPass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDMoveBasicBlocks() {
      sd_print("deleting SDMoveBasicBlocks pass\n");
    }

    bool runOnModule(Module &M) override {
      sd_print("removed thunks...\n");
      for (auto fIt = M.begin(); fIt != M.end(); fIt++) {
        std::vector<BasicBlock*> toMove;
        for (auto bbIt = fIt->begin(); bbIt != fIt->end(); bbIt ++) {
          std::string name = bbIt->getName().str();

          if (name == "sd.check.fail" || name.find("sd.fastcheck.fail") != std::string::npos) {
            toMove.push_back(bbIt);
          }
        }

        Function::BasicBlockListType &bbs = fIt->getBasicBlockList();
        for (auto bb : toMove) {
          std::cerr << "Moving " << bb->getName().str() << " to end in " << 
            fIt->getName().str() << "\n";
          bbs.remove(bb);
          bbs.insert(bbs.end(), bb);
        }
      }
      return true;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override { }

  private:
  };
}

char SDMoveBasicBlocks::ID = 0;

INITIALIZE_PASS(SDMoveBasicBlocks, "sdmovbb", "Move some (hopefully rarely ran) basic blocks out of the way.", false, false)

ModulePass* llvm::createSDMoveBasicBlocksPass() {
  return new SDMoveBasicBlocks();
}
/// ----------------------------------------------------------------------------
/// implementation
/// ----------------------------------------------------------------------------
