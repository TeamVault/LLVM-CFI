#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO.h"

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

// you have to modify the following 4 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManagerBuilder.cpp

using namespace llvm;

namespace {
  /**
   * Pass for inserting the return address intrinsic
   */

  static Function* createPrintfPrototype(Module* module) {
    std::vector<llvm::Type*> argTypes;
    argTypes.push_back(Type::getInt8PtrTy(module->getContext()));

    llvm::FunctionType* printf_type = FunctionType::get(
      /* ret type: int32 */ llvm::Type::getInt32Ty(module->getContext()),
      /* args: int8* */ argTypes,
      /* var args */ true);

    return cast<Function>(module->getOrInsertFunction("printf", printf_type));
  }

  static Constant* createPrintfFormatString(std::string funcName, IRBuilder<> &builder){
    GlobalVariable* formatStr = builder.CreateGlobalString(
      funcName + " returns to %p\n", "SafeDispatchPrintfFormatStr");
    
    // Source: https://stackoverflow.com/questions/28168815/adding-a-function-call-in-my-ir-code-in-llvm
    ConstantInt* zero = builder.getInt32(0);
    Constant* indices[] = {zero, zero};
    Constant* formatStringRef = ConstantExpr::getGetElementPtr(
      nullptr, formatStr, indices, true);

    return formatStringRef;
  }

  struct SDReturnAddress : public FunctionPass {
    static char ID;
    SDReturnAddress() : FunctionPass(ID) {
      sd_print("Initializing SDReturnAddress pass ...\n");
      initializeSDReturnAddressPass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDReturnAddress() {
      sd_print("deleting SDReturnAddress pass\n");
    }

    virtual bool runOnFunction(Function &F) override {
      for (auto &B : F) {
        sd_print("P7. Inserting retAddr (%s)\n", F.getName());

        Module* module = F.getParent();
        IRBuilder<> builder(&B);
        builder.SetInsertPoint(&B, ++builder.GetInsertPoint());

        //Create returnAddr intrinsic call
        Function* retAddrIntrinsic = Intrinsic::getDeclaration(
          F.getParent(), Intrinsic::returnaddress);
        ConstantInt* zero = builder.getInt32(0);
        auto retAddrCall = builder.CreateCall(retAddrIntrinsic, zero);

        //Create printf call
        auto printfPrototype = createPrintfPrototype(module);
        auto printfFormatString = createPrintfFormatString(F.getName().str(), builder);
        ArrayRef<Value*> args = {printfFormatString, retAddrCall};
        builder.CreateCall(printfPrototype, args);

        // We modified the code.
        return true;
      }
      return false;
    }

    const char *getPassName() const override {
      return "Safe Dispatch Return Address";
    }

  };
}

char SDReturnAddress::ID = 0;

INITIALIZE_PASS(SDReturnAddress, "sdRetAdd", "Insert return intrinsic.", false, false)

FunctionPass* llvm::createSDReturnAddressPass() {
  return new SDReturnAddress();
}
