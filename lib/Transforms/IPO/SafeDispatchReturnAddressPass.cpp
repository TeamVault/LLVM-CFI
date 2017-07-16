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
      sd_print("P??. Started running SDReturnAddress pass ...\n");

      if (F.getName() != "_ZN1E1fEv")
        return false;

      for (auto &B : F) {
        for (auto &I : B ) {
          if (!isa<ReturnInst>(I))
            continue;

          sd_print("P7. Inserting retAddr (%s)\n", F.getName());

          Module *module = F.getParent();
          IRBuilder<> builder(&I);
          //builder.SetInsertPoint(&B, ++builder.GetInsertPoint());

          //Create returnAddr intrinsic call
          Function *retAddrIntrinsic = Intrinsic::getDeclaration(
                  F.getParent(), Intrinsic::returnaddress);
          ConstantInt *zero = builder.getInt32(0);

          auto retAddrCall = builder.CreateCall(retAddrIntrinsic, zero);

          auto int64Ty = Type::getInt64Ty(F.getParent()->getContext());
          auto retAddr = builder.CreatePtrToInt(retAddrCall, int64Ty);

          errs() << "minCheck";
          auto globalMin = getOrCreateGlobal(F.getParent(), B, "min");
          auto minPtr = builder.CreateLoad(globalMin);
          auto min = builder.CreatePtrToInt(minPtr, int64Ty);
          //auto min = builder.CreateLoad(int64Ty, globalMin,  "sd_range_min");


          auto globalMax = getOrCreateGlobal(F.getParent(), B, "max");
          auto maxPtr = builder.CreateLoad(globalMax);
          auto max = builder.CreatePtrToInt(maxPtr, int64Ty);
          //auto max = builder.CreateLoad(int64Ty, globalMax, "sd_range_max");

          errs() << *minPtr->getType() << " "  << *retAddr->getType();
          auto diff = builder.CreateSub(retAddr, min);
          errs() << "diffCheck";
          auto width = builder.CreateSub(max, min);
          auto check = builder.CreateICmpULE(diff, width);

          //Create printf call
          auto printfPrototype = createPrintfPrototype(module);
          auto printfFormatString = createPrintfFormatString(F.getName().str(), builder);
          ArrayRef < Value * > args = {printfFormatString, check};
          builder.CreateCall(printfPrototype, args);

          // We modified the code.
          sd_print("P??. Finished running SDReturnAddress pass...\n");
        }
      }
      sd_print("P??. Finished running SDReturnAddress pass...\n");
      return true;
    }

    const char *getPassName() const override {
      return "Safe Dispatch Return Address";
    }

    GlobalVariable* getOrCreateGlobal(Module* M, BasicBlock &BB, StringRef suffix) {
      Twine name = "_SD_RANGESTUB_ZTV1E_" + suffix;
      auto global = M->getGlobalVariable(name.str());
      if (global != nullptr)
        return global;

      errs() << "new global with name: " << name.str() << "\n";
      //auto type = llvm::Type::getInt64PtrTy(M->getContext());

      PointerType* labelType = llvm::Type::getInt8PtrTy(M->getContext()); // TODO MATT: second arg?

      //ArrayType* arrayOfLabelType = ArrayType::get(labelType, 1);
      auto newGlobal = new GlobalVariable(*M, labelType, false, GlobalVariable::ExternalLinkage, nullptr, name);

      //std::vector<Constant*> vector;
      //vector.push_back(BlockAddress::get(&BB));
      //auto init = ConstantArray::get(arrayOfLabelType, array);


      auto nullPointer = ConstantPointerNull::get(labelType);
      newGlobal->setInitializer(nullPointer);
      return newGlobal;
    }


    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }

  };
}

char SDReturnAddress::ID = 0;

INITIALIZE_PASS(SDReturnAddress, "sdRetAdd", "Insert return intrinsic.", false, false)

FunctionPass* llvm::createSDReturnAddressPass() {
  return new SDReturnAddress();
}
