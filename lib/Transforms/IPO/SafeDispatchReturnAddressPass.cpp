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
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

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

  static std::string itaniumConstructorTokens[] = {"C0Ev", "C1Ev", "C2Ev", "D0Ev", "D1Ev", "D2Ev"};

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
      funcName + " retAddr: %p, min: %p, max: %p -> %d\n", "SafeDispatchPrintfFormatStr");
    
    // Source: https://stackoverflow.com/questions/28168815/adding-a-function-call-in-my-ir-code-in-llvm
    ConstantInt* zero = builder.getInt32(0);
    Constant* indices[] = {zero, zero};
    Constant* formatStringRef = ConstantExpr::getGetElementPtr(
      nullptr, formatStr, indices, true);

    return formatStringRef;
  }

  static std::string demangleFunction(std::string functionName) {
    std::string className = "";
    bool foundDigit = false;
    bool startedWriting = false;
    for (auto c : functionName){
      if (isdigit(c)) {

        if (foundDigit && startedWriting) {
          return className;
        } else {
          foundDigit = true;
        }

      } else if (foundDigit) {
        startedWriting = true;
        className.push_back(c);
      }
    }
    assert(false && "Demangle failed!");
    return className;
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
      sd_print("P7. Running SDReturnAddress pass for %s\n", F.getName());
      if (!F.getName().startswith("_ZN")) {
        sd_print("Function skipped!\n");
        return false;
      }
      for (auto token : itaniumConstructorTokens) {
        if (F.getName().endswith(token)) {
          sd_print("Constructor skipped!\n");
          return false;
        }
      }

      for (auto &B : F) {
        for (auto &I : B ) {
          if (!isa<ReturnInst>(I))
            continue;

          auto className = demangleFunction(F.getName());
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

          auto globalMin = getOrCreateGlobal(F.getParent(), className + "_min");
          auto minPtr = builder.CreateLoad(globalMin);
          auto min = builder.CreatePtrToInt(minPtr, int64Ty);
          //auto min = builder.CreateLoad(int64Ty, globalMin,  "sd_range_min");


          auto globalMax = getOrCreateGlobal(F.getParent(), className + "_max");
          auto maxPtr = builder.CreateLoad(globalMax);
          auto max = builder.CreatePtrToInt(maxPtr, int64Ty);
          //auto max = builder.CreateLoad(int64Ty, globalMax, "sd_range_max");

          auto diff = builder.CreateSub(retAddr, min);
          auto width = builder.CreateSub(max, min);
          auto check = builder.CreateICmpULE(diff, width);

          //Create printf call
          auto printfPrototype = createPrintfPrototype(module);
          auto printfFormatString = createPrintfFormatString(F.getName().str(), builder);
          ArrayRef < Value * > args = {printfFormatString, retAddr, min, max, check};
          builder.CreateCall(printfPrototype, args);

          // We modified the code.
        }
      }
      return true;
    }

    const char *getPassName() const override {
      return "Safe Dispatch Return Address";
    }

    GlobalVariable* getOrCreateGlobal(Module* M, Twine suffix) {
      Twine name = "_SD_RANGESTUB_" + suffix;
      auto global = M->getGlobalVariable(name.str());
      if (global != nullptr)
        return global;

      sdLog::stream() << "New global with name: " << name.str() << "\n";
      //auto type = llvm::Type::getInt64PtrTy(M->getContext());

      PointerType* labelType = llvm::Type::getInt8PtrTy(M->getContext());
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

