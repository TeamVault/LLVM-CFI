#include "llvm/ADT/StringSet.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"
#include "llvm/Transforms/IPO/SafeDispatchReturnRange.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <llvm/IR/DebugInfoMetadata.h>
#include <fstream>
#include <llvm/IR/MDBuilder.h>

namespace llvm {
/**
 * Pass for inserting the return address intrinsic
 */

static const std::string itaniumDestructorTokens[] = {"D0Ev", "D1Ev", "D2Ev"};
static const std::string itaniumConstructorTokens[] = {"C0Ev", "C1Ev", "C2Ev"};

static Function *createPrintfPrototype(Module *module) {
  std::vector<llvm::Type *> argTypes;
  argTypes.push_back(Type::getInt8PtrTy(module->getContext()));

  llvm::FunctionType *printf_type = FunctionType::get(
          /* ret type: int32 */ llvm::Type::getInt32Ty(module->getContext()),
          /* args: int8* */ argTypes,
          /* var args */ true);

  return cast<Function>(module->getOrInsertFunction("printf", printf_type));
}

static void createPrintCall(std::string FormatString, std::vector<Value*> Args, IRBuilder<> &builder, Module* M) {
  GlobalVariable *formatStrGV = builder.CreateGlobalString(FormatString, "SafeDispatchFormatStr");
  ConstantInt *zero = builder.getInt32(0);
  ArrayRef<Value*> indices({zero, zero});
  Value *formatStringRef = builder.CreateGEP(nullptr, formatStrGV, indices);

  Args.insert(Args.begin(), formatStringRef);
  ArrayRef<Value*> ArgsRef = ArrayRef<Value*>(Args);

  Function *PrintProto = createPrintfPrototype(M);
  builder.CreateCall(PrintProto, ArgsRef);
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

    // get analysis results
    CHA = &getAnalysis<SDBuildCHA>();
    ReturnRange = &getAnalysis<SDReturnRange>();
    StaticFunctions = ReturnRange->getStaticFunctions();
    functionID = CHA->getMaxID() + 1;
    sdLog::log() << "start id for static functions: " << functionID << "\n";

    // init statistics
    std::set<StringRef> FunctionsMarkedStatic;
    std::set<StringRef> FunctionsMarkedVirtual;
    std::set<StringRef> FunctionsMarkedNoCaller;
    std::set<StringRef> FunctionsMarkedNoReturn;
    std::set<StringRef> FunctionsMarkedBlackListed;

    int NumberOfTotalChecks = 0;

    for (auto &F : M) {
      ProcessingInfo Info;
      Info.RangeName = F.getName();

      // do processing
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
        sdLog::errs() << "Function: " << Info.RangeName << "has no checks but is not NoReturn or blacklisted!\n";

      NumberOfTotalChecks += NumberOfChecks;
    }

    // enables the backend pass
    if (NumberOfTotalChecks > 0) {
      M.getOrInsertNamedMetadata("SD_emit_return_labels");

      // store function IDs for backend
      storeFunctionIDMap(M);
    }

    // print and store statistics
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
    AU.addRequired<SDBuildCHA>(); //  depends on SDBuildCHA pass (max virtual FunctionID)
    AU.addRequired<SDReturnRange>(); //  depends on ReturnRange pass  (StaticFunctions with Caller)
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
  SDBuildCHA *CHA;
  SDReturnRange *ReturnRange;

  const StringSet<> *StaticFunctions;
  std::map<std::string, uint64_t> FunctionIDMap;
  uint64_t functionID;

  int processFunction(Function &F, ProcessingInfo &Info) {
    if (isBlackListedFunction(F)) {
      Info.insert(BlackListed);
      return 0;
    }

    if (isVirtualFunction(F)) {
      return processVirtualFunction(F, Info);
    }

    if (isStaticFunction(F, Info)) {
      return processStaticFunction(F, Info);
    }

    llvm_unreachable("Function was not processed!");
  }

  bool isBlackListedFunction(const Function &F) {
    if (F.getName().startswith("__")
           || F.getName().startswith("llvm.")
           || F.getName() == "_Znwm"
           || F.getName() == "main"
           || F.getName().startswith("_GLOBAL_")) {
      return true;
    }

    return false;
  }

  bool isStaticFunction(const Function &F, ProcessingInfo Info) const {
    if (StaticFunctions->find(F.getName()) == StaticFunctions->end())
      Info.insert(NoCaller);
    return true;
  }

  bool isVirtualFunction(const Function &F) const {
    if (!F.getName().startswith("_Z")) {
      return false;
    }

    for (auto &Token : itaniumConstructorTokens) {
      if (F.getName().endswith(Token)) {
        return false;
      }
    }

    if (F.getName().startswith("_ZTh")) {
      return true;
    }

    return !(CHA->getFunctionID(F.getName()).empty());
  }

  int processStaticFunction(Function &F, ProcessingInfo &Info) {
    FunctionIDMap[F.getName()] = functionID;
    int NumberOfChecks = generateCompareChecks(F, functionID);

    sdLog::log() << "Function (static): " << F.getName()
                 << " gets ID: " << functionID
                 << " (Checks: " << NumberOfChecks << ")\n";

    functionID++;

    Info.insert(Static);
    if (NumberOfChecks == 0)
      Info.insert(NoReturn);

    return NumberOfChecks;
  }

  int processVirtualFunction(Function &F, ProcessingInfo &Info) {
    StringRef functionName = F.getName();
    std::string functionNameString = functionName.str();

    // check for thunk (sets functionNameString to the function the thunk is used for)
    if (functionName.startswith("_ZTh")) {
      // remove the "_ZTh" part from the name and replace it with "_Z"
      auto splits = functionName.drop_front(1).split("_");
      functionNameString = ("_Z" + splits.second).str();

      if (CHA->getFunctionID(functionNameString).empty()) {
        sdLog::warn() << "Thunk to function conversion failed. Skipping thunk: " << functionName << "\n";
        return 0;
      }
    }

    std::vector<uint64_t> IDs = CHA->getFunctionID(functionNameString);
    assert(!IDs.empty() && "Unknown Virtual Function!");
    FunctionIDMap[F.getName()] = IDs[0];
    int NumberOfChecks = generateRangeChecks(F, IDs);

    sdLog::log() << "Function (virtual): " << F.getName()
                 << " (Checks: " << NumberOfChecks << ")\n";

    Info.insert(Virtual);
    if (NumberOfChecks == 0)
      Info.insert(NoReturn);

    return NumberOfChecks;
  }

  int generateRangeChecks(Function &F, std::vector<uint64_t> IDs) {
    // Collect all return statements (usually just a single one) first.
    // We need to do this first, because inserting checks invalidates the Instruction-Iterator.
    std::vector<Instruction *> Returns;
    for (auto &B : F) {
      for (auto &I : B) {
        if (isa<ReturnInst>(I)) {
          Returns.push_back(&I);
        }
      }
    }

    Module *M = F.getParent();
    int count = 0;
    for (auto RI : Returns) {
      // Inserting check before RI is executed.
      IRBuilder<> builder(RI);

      //Create 'returnaddress'-intrinsic call
      Function *ReturnAddressFunc = Intrinsic::getDeclaration(
              F.getParent(), Intrinsic::returnaddress);

      // Some constants we need
      ConstantInt *zero = builder.getInt32(0);
      ConstantInt *offsetFirstNOP = builder.getInt32(3);
      ConstantInt *offsetSecondNOP = builder.getInt32(3 + 7);
      ConstantInt *bitMask = builder.getInt32(0x7FFFF);
      auto int64Ty = Type::getInt64Ty(M->getContext());
      auto int32PtrTy = Type::getInt32PtrTy(M->getContext());

      // Get return address
      auto ReturnAddress = builder.CreateCall(ReturnAddressFunc, zero);

      // Load minID from first NOP (extract actual value using the bit mask)
      auto minPtr = builder.CreateGEP(ReturnAddress, offsetFirstNOP);
      auto min32Ptr = builder.CreatePointerCast(minPtr, int32PtrTy);
      auto minMasked = builder.CreateLoad(min32Ptr);
      auto minID = builder.CreateAnd(minMasked, bitMask);

      // Load width from second NOP (extract actual value using the bit mask)
      auto widthPtr = builder.CreateGEP(ReturnAddress, offsetSecondNOP);
      auto width32Ptr = builder.CreatePointerCast(widthPtr, int32PtrTy);
      auto widthMasked = builder.CreateLoad(width32Ptr);
      auto width = builder.CreateAnd(widthMasked, bitMask);

      if (IDs.size() != 1) {
        // Diamond detected
        sdLog::stream() << F.getName() << "has " << IDs.size() << " IDs!\n";
      }

      // Build first check
      ConstantInt *IDValue = builder.getInt32(IDs[0]);
      auto diff = builder.CreateSub(IDValue, minID);
      auto check = builder.CreateICmpULE(diff, width);

      // Branch to Success or Next
      // Next is used either for additional checks or for the fail block.
      TerminatorInst *Success, *Next;
      MDBuilder MDB(F.getContext());
      SplitBlockAndInsertIfThenElse(check, RI, &Success, &Next,
                                MDB.createBranchWeights(
                                        std::numeric_limits<uint32_t>::max(),
                                        std::numeric_limits<uint32_t>::min()));

      // Prepare for additional checks
      BasicBlock *SuccessBlock = Success->getParent();
      BasicBlock *CurrentBlock = Next->getParent();
      Next->eraseFromParent();
      assert(CurrentBlock->empty() && "Current Block still contains Instructions!");

      // Create additional ID checks
      for (int i = 1; i < IDs.size(); ++i) {
        // Build check
        builder.SetInsertPoint(CurrentBlock);
        IDValue = builder.getInt32(IDs[i]);
        diff = builder.CreateSub(IDValue, minID);
        check = builder.CreateICmpULE(diff, width);

        // New block in case this ID check fails
        CurrentBlock = BasicBlock::Create(F.getContext(), "", CurrentBlock->getParent());
        builder.CreateCondBr(check, SuccessBlock, CurrentBlock);
      }

      // Handle external call case
      //TODO MATT: fix constant for external call
      builder.SetInsertPoint(CurrentBlock);
      ConstantInt *memRange = builder.getInt64(0x2000000);
      auto returnAddressAsInt = builder.CreatePtrToInt(ReturnAddress, int64Ty);
      auto checkExternal = builder.CreateICmpUGT(returnAddressAsInt, memRange);
      CurrentBlock = BasicBlock::Create(F.getContext(), "", CurrentBlock->getParent());
      builder.CreateCondBr(checkExternal, SuccessBlock, CurrentBlock);

      if (F.hasAddressTaken()) {
        // Handle indirect call case
        builder.SetInsertPoint(CurrentBlock);
        ConstantInt *indirectMagicNumber = builder.getInt32(0x7EFEF);
        auto checkIndirectCall = builder.CreateICmpEQ(minID, indirectMagicNumber);
        CurrentBlock = BasicBlock::Create(F.getContext(), "", CurrentBlock->getParent());
        builder.CreateCondBr(checkIndirectCall, SuccessBlock, CurrentBlock);

        builder.SetInsertPoint(CurrentBlock);
        ConstantInt *unknownMagicNumber = builder.getInt32(0x7FFFF);
        auto checkUnknownCall = builder.CreateICmpEQ(minID, unknownMagicNumber);
        CurrentBlock = BasicBlock::Create(F.getContext(), "", CurrentBlock->getParent());
        builder.CreateCondBr(checkUnknownCall, SuccessBlock, CurrentBlock);
      }

      // Build the success block
      builder.SetInsertPoint(Success);
      std::string formatStringSuccess = "'\n";
      std::vector<Value *> argsSuccess;
      //createPrintCall(formatStringSuccess, argsSuccess, builder, M);

      // Build the fail block (CurrentBlock is the block after the last check failed)
      builder.SetInsertPoint(CurrentBlock);
      std::string formatStringFail = F.getName().str() + " virtual ID %d (got %d,%d from %p) -> %d\n";
      std::vector<Value *> argsFail = {IDValue, minID, width, ReturnAddress, check};
      createPrintCall(formatStringFail, argsFail, builder, M);

      // Build the fail case TerminatorInst (quit program or continue after backward-edge violation?)
      builder.CreateBr(RI->getParent());
      //builder.CreateUnreachable();

      count++;
    }
    return count;
  }

  int generateCompareChecks(Function &F, uint64_t ID) {
    // Collect all return statements (usually just a single one) first.
    // We need to do this first, because inserting checks invalidates the Instruction-Iterator.
    std::vector<Instruction *> Returns;
    for (auto &B : F) {
      for (auto &I : B) {
        if (isa<ReturnInst>(I)) {
          Returns.push_back(&I);
        }
      }
    }

    Module *M = F.getParent();
    int count = 0;
    for (auto RI : Returns) {
      // Inserting check before RI is executed.
      IRBuilder<> builder(RI);

      //Create 'returnaddress'-intrinsic call
      Function *ReturnAddressFunc = Intrinsic::getDeclaration(
              F.getParent(), Intrinsic::returnaddress);

      // Some constants we need
      ConstantInt *zero = builder.getInt32(0);
      ConstantInt *offsetFirstNOP = builder.getInt32(3);
      ConstantInt *bitMask = builder.getInt32(0x7FFFF);
      auto int64Ty = Type::getInt64Ty(M->getContext());
      auto int32PtrTy = Type::getInt32PtrTy(M->getContext());

      // Get return address
      auto ReturnAddress = builder.CreateCall(ReturnAddressFunc, zero);

      // Load minID from first NOP (extract actual value using the bit mask)
      auto minPtr = builder.CreateGEP(ReturnAddress, offsetFirstNOP);
      auto min32Ptr = builder.CreatePointerCast(minPtr, int32PtrTy);
      auto minMasked = builder.CreateLoad(min32Ptr);
      auto minID = builder.CreateAnd(minMasked, bitMask);

      // Build ID compare check
      ConstantInt *IDValue = builder.getInt32(ID);
      auto check = builder.CreateICmpEQ(IDValue, minID);

      // Branch to Success or Fail
      MDBuilder MDB(F.getContext());
      TerminatorInst *Success, *Fail;
      SplitBlockAndInsertIfThenElse(check,
                                    RI,
                                    &Success,
                                    &Fail,
                                    MDB.createBranchWeights(
                                            std::numeric_limits<uint32_t>::max(),
                                            std::numeric_limits<uint32_t>::min()));

      // Build the success block
      std::string formatStringSuccess = ".\n";
      std::vector<Value *> argsSuccess;
      builder.SetInsertPoint(Success);
      //createPrintCall(formatStringSuccess, argsSuccess, builder, M);


      // Build the fail block
      builder.SetInsertPoint(Fail);

      // Handle external call case
      //TODO MATT: fix constant for external call
      ConstantInt *memRange = builder.getInt64(0x2000000);
      auto returnAddressAsInt = builder.CreatePtrToInt(ReturnAddress, int64Ty);
      auto checkExternal = builder.CreateICmpUGT(returnAddressAsInt, memRange);
      TerminatorInst *IsExternal, *IsNotExternal;
      SplitBlockAndInsertIfThenElse(checkExternal, Fail, &IsExternal, &IsNotExternal);


      builder.SetInsertPoint(IsExternal);
      std::string formatStringOutOfSection = F.getName().str() + " external %p\n";
      std::vector<Value *> argsOutOfSection = {ReturnAddress};
      createPrintCall(formatStringOutOfSection, argsOutOfSection, builder, M);
      builder.SetInsertPoint(IsNotExternal);


      if (F.hasAddressTaken()) {
        // Handle indirect call case
        ConstantInt *indirectMagicNumber = builder.getInt32(0x7EFEF);
        auto checkIndirectCall = builder.CreateICmpEQ(minID, indirectMagicNumber);
        TerminatorInst *IsIndirectCall, *IsNotIndirectCall;
        SplitBlockAndInsertIfThenElse(checkIndirectCall,
                                      IsNotExternal,
                                      &IsIndirectCall,
                                      &IsNotIndirectCall);

        /*
        builder.SetInsertPoint(IsIndirectCall);
        std::string formatStringIndirect = F.getName().str() + " indirect call from %p\n";
        std::vector<Value *> argsIndirect = {ReturnAddress};
        createPrintCall(formatStringIndirect, argsIndirect, builder, M);
         */
        builder.SetInsertPoint(IsNotIndirectCall);

        ConstantInt *unknownMagicNumber = builder.getInt32(0x7FFFF);
        auto checkUnknownCall = builder.CreateICmpEQ(minID, unknownMagicNumber);
        TerminatorInst *IsUnknown, *IsNotUnknown;
        SplitBlockAndInsertIfThenElse(checkUnknownCall,
                                      IsNotIndirectCall,
                                      &IsUnknown,
                                      &IsNotUnknown);
        builder.SetInsertPoint(IsNotUnknown);
      }

      /*
      // [retAddr] is NOP?
      auto nopPtr = builder.CreateGEP(ReturnAddress, zero);
      auto nop = builder.CreateLoad(nopPtr);
      ConstantInt *nopOpcode = builder.getInt8(0x0F);
      auto checkNOP = builder.CreateICmpNE(nop, nopOpcode);
      auto FailNOP = SplitBlockAndInsertIfThen(checkNOP, IsNotIndirectCall, false);
      builder.SetInsertPoint(FailNOP);
      */

      // Build the final fail block (no case matched -> backwards-edge violation)
      std::string formatStringFail = F.getName().str() + " static ID %d (got %d from %p) -> %d\n";
      std::vector<Value *> argsFail = {IDValue, minID, ReturnAddress, check};
      createPrintCall(formatStringFail, argsFail, builder, M);
      // Quit program after backward-edge violation
      //builder.CreateUnreachable();

      count++;
    }
    return count;
  }

  void storeFunctionIDMap(Module &M) {
    sdLog::stream() << "Store all function IDs for module: " << M.getName() << "\n";
    std::ofstream Outfile("./_SD_FunctionIDMap.txt");

    for (auto &mapEntry : FunctionIDMap) {
      Outfile << mapEntry.first << "," << mapEntry.second << "\n";
    }

    Outfile.close();

    int number = 0;
    auto outName = "./_SD_FunctionIDMap" + std::to_string(number);
    std::ifstream infile(outName);
    while(infile.good()) {
      number++;
      outName = "./_SD_FunctionIDMap" + std::to_string(number);
      infile = std::ifstream(outName);
    }

    std::ifstream src("./_SD_FunctionIDMap", std::ios::binary);
    std::ofstream dst(outName, std::ios::binary);
    dst << src.rdbuf();
  }
};

char SDReturnAddress::ID = 0;

INITIALIZE_PASS(SDReturnAddress, "sdRetAdd", "Insert return intrinsic.", false, false)

llvm::ModulePass *llvm::createSDReturnAddressPass() {
  return new SDReturnAddress();
}

}
