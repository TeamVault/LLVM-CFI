#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
using namespace llvm;

#define DEBUG_TYPE "cc"

namespace {
  struct ChangeConstant : public BasicBlockPass {
    static char ID; // Pass identification, replacement for typeid

    ChangeConstant() : BasicBlockPass(ID) {
      initializeChangeConstantPass(*PassRegistry::getPassRegistry());
    }

    bool runOnBasicBlock(BasicBlock &BB) override {
      for(BasicBlock::iterator instItr = BB.begin(); instItr != BB.end(); instItr++) {
        Instruction* inst = instItr;
        if (inst->getOpcode() == 28) { // store operation
          StoreInst* storeInst = dyn_cast_or_null<StoreInst>(inst);
          assert(storeInst);

          Value* storeVal = storeInst->getOperand(0);
          ConstantInt* constIntVal = dyn_cast_or_null<ConstantInt>(storeVal);

          if(constIntVal) {
            if (*(constIntVal->getValue().getRawData()) == 42) {
              errs() << "this has the magic one\n";
              IntegerType* intType = constIntVal->getType();
              inst->setOperand(0, ConstantInt::get(intType, 43, false));
              inst->dump();
            }
          }
        }
      }

      return false;
    }

  };
}

char ChangeConstant::ID = 0;

INITIALIZE_PASS(ChangeConstant, "cc", "Change Constant", false, false)

BasicBlockPass* llvm::createChangeConstantPass() {
  return new ChangeConstant();
}
