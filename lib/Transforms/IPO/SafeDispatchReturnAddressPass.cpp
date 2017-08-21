#include "llvm/ADT/StringSet.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"
#include "llvm/Transforms/IPO/SafeDispatchReturnRange.h"
#include "llvm/Transforms/IPO/SafeDispatchMD.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <cxxabi.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <fstream>

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

static Constant *createPrintfFormatString2(std::string funcName, IRBuilder<> &builder) {
  GlobalVariable *formatStr = builder.CreateGlobalString(
          funcName + " virtual ID %d (%d, %d) -> %d\n", "SafeDispatchPrintfFormatStr");

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
  return className;
}


class SDReturnAddress : public ModulePass {
public:
  static char ID;

  SDReturnAddress() : ModulePass(ID), GlobalsCreated() {
    sdLog::stream() << "initializing SDReturnAddress pass ...\n";
    initializeSDReturnAddressPass(*PassRegistry::getPassRegistry());
  }

  virtual ~SDReturnAddress() {
    sdLog::stream() << "deleting SDReturnAddress pass\n";
  }

  bool runOnModule(Module &M) override {
    // Get the results from the ReturnRange pass.
    CHA = &getAnalysis<SDBuildCHA>();
    ReturnRange = &getAnalysis<SDReturnRange>();
    StaticFunctions = ReturnRange->getStaticFunctions();

    sdLog::blankLine();
    sdLog::stream() << "P7b. Started running the SDReturnAddress pass ..." << sdLog::newLine << "\n";

    std::set<StringRef> FunctionsMarkedStatic;
    std::set<StringRef> FunctionsMarkedVirtual;
    std::set<StringRef> FunctionsMarkedNoCaller;
    std::set<StringRef> FunctionsMarkedNoReturn;
    std::set<StringRef> FunctionsMarkedBlackListed;

    int NumberOfTotalChecks = 0;
    for (auto &F : M) {
      ProcessingInfo Info;
      Info.RangeName = F.getName();
      int NumberOfChecks = processFunction(F, Info);

      bool InfoValidatesNoChecks = false;
      for (auto &Entry : Info.Flags) {
        switch (Entry) {
          case Static:
            FunctionsMarkedStatic.insert(Info.RangeName);
            break;
          case Virtual:
            FunctionsMarkedVirtual.insert(Info.RangeName);
            break;
          case NoCaller:
            errs() << "NO CALLER" << Info.RangeName << "\n";
            FunctionsMarkedNoCaller.insert(Info.RangeName);
            break;
          case NoReturn:
            FunctionsMarkedNoReturn.insert(Info.RangeName);
            InfoValidatesNoChecks = true;
            break;
          case BlackListed:
            FunctionsMarkedBlackListed.insert(Info.RangeName);
            InfoValidatesNoChecks = true;
            break;
        }
      }

      if (NumberOfChecks == 0 && !InfoValidatesNoChecks)
        sdLog::errs() << "Function: " << Info.RangeName << "has no checks and is not marked with NoReturn!\n";

      NumberOfTotalChecks += NumberOfChecks;
    }

    if (NumberOfTotalChecks > 0) {
      M.getOrInsertNamedMetadata("SD_emit_return_labels");
    }

    sdLog::stream() << sdLog::newLine << "P7b. SDReturnAddress statistics:" << "\n";
    sdLog::stream() << "Total number of checks: " << NumberOfTotalChecks << "\n";
    sdLog::stream() << "Total number of static functions: " << FunctionsMarkedStatic.size() << "\n";
    sdLog::stream() << "Total number of virtual functions: " << FunctionsMarkedVirtual.size() << "\n";
    sdLog::stream() << "Total number of blacklisted functions: " << FunctionsMarkedBlackListed.size() << "\n";
    sdLog::stream() << "Total number of functions without return: " << FunctionsMarkedNoReturn.size() << "\n";

    storeStatistics(M, NumberOfTotalChecks,
                    FunctionsMarkedStatic,
                    FunctionsMarkedVirtual,
                    FunctionsMarkedNoCaller,
                    FunctionsMarkedNoReturn,
                    FunctionsMarkedBlackListed);

    sdLog::stream() << sdLog::newLine << "P7b. Finished running the SDReturnAddress pass ..." << "\n";
    sdLog::blankLine();


    return NumberOfTotalChecks > 0;
  }

  void storeStatistics(Module &M, int NumberOfTotalChecks,
                       std::set<StringRef> &FunctionsMarkedStatic,
                       std::set<StringRef> &FunctionsMarkedVirtual,
                       std::set<StringRef> &FunctionsMarkedNoCaller,
                       std::set<StringRef> &FunctionsMarkedNoReturn,
                       std::set<StringRef> &FunctionsMarkedBlackListed) {

    sdLog::stream() << "Store statistics for module: " << M.getName() << "\n";

    int number = 0;
    auto outName = "./SD_Stats" + std::to_string(number);
    std::ifstream infile(outName);
    while(infile.good()) {
      number++;
      outName = "./SD_Stats" + std::to_string(number);
      infile = std::ifstream(outName);
    }
    std::ofstream Outfile(outName);

    std::ostream_iterator <std::string> OutIterator(Outfile, "\n");
    Outfile << "Total number of checks: " << NumberOfTotalChecks << "\n";
    Outfile << "\n";

    Outfile << "### Static function checks: " << FunctionsMarkedStatic.size() << "\n";
    std::copy(FunctionsMarkedStatic.begin(), FunctionsMarkedStatic.end(), OutIterator);
    Outfile << "##\n";

    Outfile << "### Virtual function checks: " << FunctionsMarkedVirtual.size() << "\n";
    std::copy(FunctionsMarkedVirtual.begin(), FunctionsMarkedVirtual.end(), OutIterator);
    Outfile << "##\n";

    Outfile << "### Blacklisted functions: " << FunctionsMarkedBlackListed.size() << "\n";
    std::copy(FunctionsMarkedBlackListed.begin(), FunctionsMarkedBlackListed.end(), OutIterator);
    Outfile << "##\n";

    Outfile << "### Without return: " << FunctionsMarkedNoReturn.size() << "\n";
    std::copy(FunctionsMarkedNoReturn.begin(), FunctionsMarkedNoReturn.end(), OutIterator);
    Outfile << "##\n";

    Outfile << "### Without caller: " << FunctionsMarkedNoCaller.size() << "\n";
    std::copy(FunctionsMarkedNoCaller.begin(), FunctionsMarkedNoCaller.end(), OutIterator);
    Outfile << "##\n";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SDBuildCHA>(); //  depends on SDBuildCHA pass
    AU.addRequired<SDReturnRange>(); //  depends on ReturnRange pass
    AU.setPreservesAll();
  }

  enum ProcessingInfoFlags {
    Static, Virtual, NoCaller, NoReturn, BlackListed
  };

  struct ProcessingInfo {
    std::set<ProcessingInfoFlags> Flags;
    StringRef RangeName;

    void insert(ProcessingInfoFlags Flag){
      Flags.insert(Flag);
    }

    bool hasFlag(ProcessingInfoFlags Flag) {
      return Flags.find(Flag) != Flags.end();
    }
  };

private:
  const StringSet<> *StaticFunctions;
  SDBuildCHA *CHA;
  SDReturnRange *ReturnRange;
  std::set<std::string> GlobalsCreated;

  int processFunction(Function &F, ProcessingInfo &Info) {
    if (isBlackListedFunction(F)) {
      Info.insert(BlackListed);
      return 0;
    }

    if (isKnownVirtualFunction(F)) {
      return processVirtualFunction(F, Info);
    }

    if (isPotentialStaticFunction(F, Info)) {
      return processStaticFunction(F, Info);
    }

    llvm_unreachable("Function was not processed!");
  }

  bool isBlackListedFunction(const Function &F) {
    return F.getName().startswith("__")
           || F.getName().startswith("llvm.")
           || F.getName() == "_Znwm"
           || F.getName() == "main";
  }

  bool isPotentialStaticFunction(const Function &F, ProcessingInfo Info) const {
    if (StaticFunctions->find(F.getName()) == StaticFunctions->end())
      Info.insert(NoCaller);
    return true;
  }

  bool isKnownVirtualFunction(const Function &F) const {
    if (!F.getName().startswith("_Z")) {
      return false;
    }

    for (auto &Token : itaniumConstructorTokens) {
      if (F.getName().endswith(Token)) {
        return false;
      }
    }
    return !(CHA->getFunctionID(F.getName()).empty());
  }

  int processStaticFunction(Function &F, ProcessingInfo &Info) {
    int NumberOfChecks = generateReturnChecks(F, F.getName());
    Info.insert(Static);
    if (NumberOfChecks == 0)
      Info.insert(NoReturn);

    return NumberOfChecks;
  }

  int processVirtualFunction(Function &F, ProcessingInfo &Info) {
    std::vector<uint64_t> IDs = CHA->getFunctionID(F.getName());
    assert(!IDs.empty() && "Unknown Virtual Function!");

    int NumberOfChecks = generateReturnChecks2(F, IDs);

    sdLog::log() << "Function (virtual): " << F.getName()
                 << " in class "
                 << " (Checks: " << NumberOfChecks << ")\n";

    Info.insert(Virtual);
    if (NumberOfChecks == 0)
      Info.insert(NoReturn);

    return NumberOfChecks;
  }

  int generateReturnChecks2(Function &F, std::vector<uint64_t> IDs) {
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
        ConstantInt *offset = builder.getInt32(3);
        ConstantInt *offset2 = builder.getInt32(3 + 7);
        ConstantInt *bitPattern = builder.getInt32(0x7FFFF);

        auto int64Ty = Type::getInt64Ty(F.getParent()->getContext());
        auto int32PtrTy = Type::getInt32PtrTy(F.getParent()->getContext());

        auto retAddrCall = builder.CreateCall(retAddrIntrinsic, zero);

        auto minPtr = builder.CreateGEP(retAddrCall, offset);
        auto min32Ptr = builder.CreatePointerCast(minPtr, int32PtrTy);
        auto min = builder.CreateLoad(min32Ptr);
        auto minFixed = builder.CreateAnd(min, bitPattern);

        auto widthPtr = builder.CreateGEP(retAddrCall, offset2);
        auto width32Ptr = builder.CreatePointerCast(widthPtr, int32PtrTy);
        auto width = builder.CreateLoad(width32Ptr);
        auto widthFixed = builder.CreateAnd(width, bitPattern);

        for (auto &ID : IDs) {
          //Create printf call
          ConstantInt *IDValue = builder.getInt32(ID);
          auto diff = builder.CreateSub(IDValue, minFixed);
          auto check = builder.CreateICmpULE(diff, widthFixed);

          auto printfPrototype = createPrintfPrototype(module);
          auto printfFormatString = createPrintfFormatString2(F.getName(), builder);
          ArrayRef<Value *> args = {printfFormatString, IDValue, minFixed, widthFixed, check};
          auto Call = builder.CreateCall(printfPrototype, args);
          count++;
        }
      }
    }
    return count;
  }

  int generateReturnChecks(Function &F, Twine CheckName) {
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

        auto globalMin = getOrCreateGlobal(F.getParent(), CheckName + "_min");
        auto minPtr = builder.CreateLoad(globalMin);
        auto min = builder.CreatePtrToInt(minPtr, int64Ty);

        auto globalMax = getOrCreateGlobal(F.getParent(), CheckName + "_max");
        auto maxPtr = builder.CreateLoad(globalMax);
        auto max = builder.CreatePtrToInt(maxPtr, int64Ty);

        auto diff = builder.CreateSub(retAddr, min);
        auto width = builder.CreateSub(max, min);
        auto check = builder.CreateICmpULE(diff, width);

        //Create printf call
        auto printfPrototype = createPrintfPrototype(module);
        auto printfFormatString = createPrintfFormatString(F.getName(), builder);
        ArrayRef < Value * > args = {printfFormatString, retAddr, min, max, check};
        //auto Call = builder.CreateCall(printfPrototype, args);
        count++;
      }
    }
    return count;
  }


  GlobalVariable *getOrCreateGlobal(Module *M, Twine suffix) {
    auto name = "_SD_RANGE_" + suffix;
    auto global = M->getGlobalVariable(name.str());
    if (global != nullptr) {
      if (GlobalsCreated.find(name.str()) == GlobalsCreated.end())
        sdLog::warn() << "Used global not created by this pass: " << name.str() << "\n";
      return global;
    }

    PointerType *labelType = llvm::Type::getInt8PtrTy(M->getContext());
    auto newGlobal = new GlobalVariable(*M, labelType, false, GlobalVariable::ExternalLinkage, nullptr, name);
    auto nullPointer = ConstantPointerNull::get(labelType);
    newGlobal->setInitializer(nullPointer);

    GlobalsCreated.insert(name.str());
    sdLog::stream() << "\tCreated global: " << name.str() << "\n";
    return newGlobal;
  }
};

char SDReturnAddress::ID = 0;

INITIALIZE_PASS(SDReturnAddress,
"sdRetAdd", "Insert return intrinsic.", false, false)

llvm::ModulePass *llvm::createSDReturnAddressPass() {
  return new SDReturnAddress();
}

}

