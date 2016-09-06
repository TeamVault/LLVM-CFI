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

// you have to modify the following 4 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManagerBuilder.cpp

using namespace llvm;

namespace {
  /**
   * Pass for updating the annotated instructions with the new indices
   */
  struct SDMoveBasicBlocks : public ModulePass {
    static char ID; // Pass identification, replacement for typeid

    SDMoveBasicBlocks() : ModulePass(ID) {
      sd_print("Initializing SDMoveBasicBlocks pass ...\n");
      initializeSDMoveBasicBlocksPass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDMoveBasicBlocks() {
      sd_print("deleting SDMoveBasicBlocks pass\n");
    }

    bool runOnModule(Module &M) override {
      sd_print("P6. Started reshufling basic block (bb) thunks started (SDMoveBasicsBlocks pass) ...\n");
      sd_print("P6. 1. if basic block (bb) name is sd.check.fail or sd.fastcheck.fail collect it ...\n");
      sd_print("P6. 2. remove bb from the bbs list (Function::BasicBlockListType &bbs) and insert it at the end ...\n");
      sd_print("P6. 3. so basically all bb blocks are reshufled at the end of the bbs list ...\n");
      sd_print("P6. 4. this improves runtime overhead ...\n");

      for (auto fIt = M.begin(); fIt != M.end(); fIt++) {
        std::vector<BasicBlock*> toMove; 
        for (auto bbIt = fIt->begin(); bbIt != fIt->end(); bbIt ++) {

          std::string name = bbIt->getName().str();
          if (name == "sd.check.fail" || name.find("sd.fastcheck.fail") != std::string::npos) {

            //Paul: collect the bb which will be removed
            toMove.push_back(bbIt);
          }
        }
        
        // Paul: this is an internal LLVM Function
        Function::BasicBlockListType &bbs = fIt->getBasicBlockList(); //Paul; this is a LLVM bb function list type
        for (auto bb : toMove) {
          std::cerr << "Moving " << bb->getName().str() << " to end in " << 
            fIt->getName().str() << "\n";

          //Paul: remove the bb from the bbs list 
          bbs.remove(bb); 

          //reinsert the bb at the end of the bbs list 
          bbs.insert(bbs.end(), bb); //Paul: add the bb at the end of the bbs list 
        }
      }
      
      sd_print("P6. Finished Removing thunks finished (SDMoveBasicsBlocks pass) ...\n");
      return true;
    }
    
    /*Paul: this function is used to pass information between passes. 
    In our case we do not pass any info. This is why this method is empty. 
    */
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
