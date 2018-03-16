#include "llvm/IR/MDBuilder.h"
#include "llvm/Transforms/IPO/SafeDispatchLayoutBuilder.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

using namespace llvm;

namespace {
  struct SDCleanup : public ModulePass {
    static char ID; // Pass identification, replacement for typeid

    SDCleanup() : ModulePass(ID) {
      sdLog::stream() << "Initializing SDCleanup pass\n";
      initializeSDCleanupPass(*PassRegistry::getPassRegistry());
    }

    bool runOnModule(Module &M) override {
      sdLog::stream() << "Started SDCleanup pass ...\n";

      handleSDGetVtblIndex(&M);
      handleSDGetCheckedVtbl(&M);
      handleRemainingSDGetVcallIndex(&M);
      sdLog::stream() << "Finished SDCleanup pass ...\n";
      return true;
    }

  private:
    void handleSDGetVtblIndex(Module* M);
    void handleSDGetCheckedVtbl(Module* M);
    void handleRemainingSDGetVcallIndex(Module* M);
  };
} // namespace

void SDCleanup::handleSDGetVtblIndex(Module* M) {
  Function *sd_vtbl_indexF = M->getFunction(Intrinsic::getName(Intrinsic::sd_get_vtbl_index));

  if (!sd_vtbl_indexF){
   return;
  }

  int counter = 0;
  for (const Use &U : sd_vtbl_indexF->uses()) {
    llvm::CallInst* CI = cast<CallInst>(U.getUser());
    llvm::ConstantInt* vptr = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    assert(vptr);
    CI->replaceAllUsesWith(vptr);
    CI->eraseFromParent();
    counter++;
  }
  sdLog::stream() << "Replaced " << counter << " sd.get.vtbl.index intrinsics.\n";
}

void SDCleanup::handleSDGetCheckedVtbl(Module* M) {
  Function *sd_vtbl_indexF = M->getFunction(Intrinsic::getName(Intrinsic::sd_get_checked_vptr));

  if (!sd_vtbl_indexF){
   return;
  }

  int counter = 0;
  for (const Use &U : sd_vtbl_indexF->uses()) {
    llvm::CallInst* CI = cast<CallInst>(U.getUser());
    llvm::Value* vptr = CI->getArgOperand(0);
    assert(vptr);
    CI->replaceAllUsesWith(vptr);
    CI->eraseFromParent();
    counter++;
  }
  sdLog::stream() << "Replaced " << counter << " sd.get.checked.vptr intrinsics.\n";
}

void SDCleanup::handleRemainingSDGetVcallIndex(Module* M) {
  Function *sd_vcall_indexF = M->getFunction(Intrinsic::getName(Intrinsic::sd_get_vcall_index));

  if (!sd_vcall_indexF){
   return;
  }

  int counter = 0;
  for (const Use &U : sd_vcall_indexF->uses()) {
    llvm::CallInst* CI = cast<CallInst>(U.getUser());
    llvm::ConstantInt* arg1 = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    assert(arg1);
    CI->replaceAllUsesWith(arg1);
    counter++;
  }

  if (counter > 0) {
    sdLog::stream() << "Replaced " << counter << "sd.get.vcall.index intrinsics.\n";
  }

}

char SDCleanup::ID = 0;

INITIALIZE_PASS(SDCleanup, "sdCleanup", "Cleanup sd intrinsics", false, false)

ModulePass* llvm::createSDCleanupPass() {
  return new SDCleanup();
}