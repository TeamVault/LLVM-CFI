#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <cxxabi.h>

namespace llvm {
/**
 * Pass for inserting the return address intrinsic
 */

static const std::string itaniumConstructorTokens[] = {"C0Ev", "C1Ev", "C2Ev", "D0Ev", "D1Ev", "D2Ev"};

static Function *createPrintfPrototype(Module *module) {
  std::vector<llvm::Type *> argTypes;
  argTypes.push_back(Type::getInt8PtrTy(module->getContext()));

  llvm::FunctionType *printf_type = FunctionType::get(
          /* ret type: int32 */ llvm::Type::getInt32Ty(module->getContext()),
          /* args: int8* */ argTypes,
          /* var args */ true);

  return cast<Function>(module->getOrInsertFunction("printf", printf_type));
}

static Constant *createPrintfFormatString(std::string funcName, IRBuilder<> &builder) {
  GlobalVariable *formatStr = builder.CreateGlobalString(
          funcName + " retAddr: %p, min: %p, max: %p -> %d\n", "SafeDispatchPrintfFormatStr");

  // Source: https://stackoverflow.com/questions/28168815/adding-a-function-call-in-my-ir-code-in-llvm
  ConstantInt *zero = builder.getInt32(0);
  Constant *indices[] = {zero, zero};
  Constant *formatStringRef = ConstantExpr::getGetElementPtr(
          nullptr, formatStr, indices, true);

  return formatStringRef;
}

static std::string demangleFunction(StringRef functionName) {
  int status = 0;
  std::unique_ptr<char, void (*)(void *)> res{
          abi::__cxa_demangle(functionName.str().c_str(), NULL, NULL, &status),
          std::free
  };
  if (status != 0)
    return "";

  StringRef demangledName = res.get();
  auto index = demangledName.rfind("::");
  auto className = demangledName.substr(0, index);
  errs() << className;
  return className;
}


class SDReturnAddress : public ModulePass {
public:
  static char ID;

  SDReturnAddress() : ModulePass(ID) {
    sdLog::stream() << "initializing SDReturnAddress pass ...\n";
    initializeSDReturnAddressPass(*PassRegistry::getPassRegistry());
  }

  virtual ~SDReturnAddress() {
    sdLog::stream() << "deleting SDReturnAddress pass\n";
  }

  bool runOnModule(Module &M) override {
    sdLog::blankLine();
    sdLog::stream() << "P7b. Started running the SDReturnAddress pass ..." << sdLog::newLine << "\n";

    bool hasChanged = false;
    for (auto &F : M) {
      hasChanged |= processFunction(F);
    }

    sdLog::stream() << sdLog::newLine << "P7b. Finished running the SDReturnAddress pass ..." << "\n";
    sdLog::blankLine();
    return hasChanged;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

private:
  bool processFunction(Function &F) {
    sdLog::stream() << "Function: " << F.getName();
    if (!isRelevantFunction(F))
      return false;

    std::string className = demangleFunction(F.getName());
    if (className == "") {
      sdLog::streamWithoutToken() << " -> WARNING: Skipping after error!\n";
      return false;
    }
    sdLog::streamWithoutToken() << " -> Demangled to " << className << "\n";

    int count = 0;
    for (auto &B : F) {
      for (auto &I : B) {
        if (!isa<ReturnInst>(I))
          continue;

        Module *module = F.getParent();
        IRBuilder<> builder(&I);

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
        auto printfFormatString = createPrintfFormatString(F.getName(), builder);
        ArrayRef<Value *> args = {printfFormatString, retAddr, min, max, check};
        builder.CreateCall(printfPrototype, args);
        count++;

        // We modified the code.
      }
    }
    sdLog::stream() << "\tChecks created: " << std::to_string(count) << "\n";
    sdLog::stream() << "\n";
    return true;
  }

  bool isRelevantFunction(const Function &F) const {
    if (!F.getName().startswith("_ZN")) {
      sdLog::streamWithoutToken() << " -> Function skipped!\n";
      return false;
    }

    for (auto &Token : itaniumConstructorTokens) {
      if (F.getName().endswith(Token)) {
        sdLog::streamWithoutToken() << " -> Constructor skipped!\n";
        return false;
      }
    }
    return true;
  }

    GlobalVariable* getOrCreateGlobal(Module* M, Twine suffix) {
      auto name = "_SD_RANGE_" + suffix;
      auto global = M->getGlobalVariable(name.str());
      if (global != nullptr)
        return global;

    PointerType *labelType = llvm::Type::getInt8PtrTy(M->getContext());
    auto newGlobal = new GlobalVariable(*M, labelType, false, GlobalVariable::ExternalLinkage, nullptr, name);
    auto nullPointer = ConstantPointerNull::get(labelType);
    newGlobal->setInitializer(nullPointer);

    sdLog::stream() << "\tCreated global: " << name.str() << "\n";
    return newGlobal;
  }
};

char SDReturnAddress::ID = 0;

INITIALIZE_PASS(SDReturnAddress, "sdRetAdd", "Insert return intrinsic.", false, false)

llvm::ModulePass *llvm::createSDReturnAddressPass() {
  return new SDReturnAddress();
}

}

