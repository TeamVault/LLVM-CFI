#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
using namespace llvm;

#define DEBUG_TYPE "cc"

#define STORE_OPCODE 28
#define GEP_OPCODE   29

namespace {
  struct ChangeConstant : public BasicBlockPass {
    static char ID; // Pass identification, replacement for typeid

    ChangeConstant() : BasicBlockPass(ID) {
      initializeChangeConstantPass(*PassRegistry::getPassRegistry());
    }

    bool runOnBasicBlock(BasicBlock &BB) override {
      Module* module = BB.getParent()->getParent();
      unsigned mdId = module->getMDKindID("sd.class.name");

      for(BasicBlock::iterator instItr = BB.begin(); instItr != BB.end(); instItr++) {
        Instruction* inst = instItr;

//        if (inst->getOpcode() == STORE_OPCODE) { // store operation
//          StoreInst* storeInst = dyn_cast_or_null<StoreInst>(inst);
//          assert(storeInst);

//          Value* storeVal = storeInst->getOperand(0);
//          ConstantInt* constIntVal = dyn_cast_or_null<ConstantInt>(storeVal);

//          if(constIntVal) {
//            if (*(constIntVal->getValue().getRawData()) == 42) {
//              errs() << "this has the magic one\n";
//              IntegerType* intType = constIntVal->getType();
//              inst->setOperand(0, ConstantInt::get(intType, 43, false));
//              inst->dump();
//            }
//          }
//        }

        if (inst->getOpcode() == GEP_OPCODE) {
          GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(inst);
          llvm::MDNode* mdNode = gepInst->getMetadata(mdId);
          if (mdNode) {
            gepInst->dump();
            gepInst->getOperand(1)->dump();
            StringRef className = cast<llvm::MDString>(mdNode->getOperand(0))->getString();
            errs() << className << "\n";

            gepInst->setOperand(1, ConstantInt::get(IntegerType::getInt64Ty(gepInst->getContext()),
                                                    0,
                                                    false));
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
