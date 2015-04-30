#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
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
      Function* function = BB.getParent();
      Module* module = function->getParent();

      errs() << "HELLO WORLD FROM CHANGECONSTANT !!!\n";
      errs() <<  module->getName() << ", " << function->getName() << "\n";

      return false;
    }

  };
}

char ChangeConstant::ID = 0;

INITIALIZE_PASS(ChangeConstant, "cc", "Change Constant", false, false)

BasicBlockPass* llvm::createChangeConstantPass() {
  return new ChangeConstant();
}
