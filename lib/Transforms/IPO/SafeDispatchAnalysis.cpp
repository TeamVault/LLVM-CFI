//===-- SafeDispatchReturnRange.cpp - SafeDispatch ReturnRange code ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SDReturnRange class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/SafeDispatchReturnRange.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

#include <fstream>
#include <sstream>

using namespace llvm;

static const std::string itaniumConstructorTokens[] = {"C0Ev", "C1Ev", "C2Ev"};

//TODO MATT: format properly / code duplication
static StringRef sd_getClassNameFromMD(llvm::MDNode *MDNode, unsigned operandNo = 0) {
  llvm::MDTuple *mdTuple = cast<llvm::MDTuple>(MDNode);
  assert(mdTuple->getNumOperands() > operandNo + 1);

  llvm::MDNode *nameMdNode = cast<llvm::MDNode>(mdTuple->getOperand(operandNo).get());
  llvm::MDString *mdStr = cast<llvm::MDString>(nameMdNode->getOperand(0));

  StringRef strRef = mdStr->getString();
  assert(sd_isVtableName_ref(strRef));
  return strRef;
}

static StringRef sd_getFunctionNameFromMD(llvm::MDNode *MDNode, unsigned operandNo = 0) {
  assert(MDNode->getNumOperands() > operandNo);

  llvm::MDString *mdStr = cast<llvm::MDString>(MDNode->getOperand(operandNo));

  StringRef strRef = mdStr->getString();
  return strRef;
}

static std::stringstream writeDebugLocToStream(const DebugLoc* Loc) {
  assert(Loc);

  auto *Scope = cast<MDScope>(Loc->getScope());
  std::stringstream Stream;
  Stream << Scope->getFilename().str() + ":" << Loc->getLine() << ":" << Loc->getCol();
  return Stream;
}

static uint16_t encodeType(Type* T, bool recurse = true) {
  uint16_t TypeEncoded;
  switch (T->getTypeID()) {
    case Type::TypeID::VoidTyID:
      TypeEncoded = 1;
      break;

    case Type::TypeID::IntegerTyID: {
      auto Bits = cast<IntegerType>(T)->getBitWidth();
      if (Bits <= 1) {
        TypeEncoded = 2;
      } else if (Bits <= 8) {
        TypeEncoded = 3;
      } else if (Bits <= 16) {
        TypeEncoded = 4;
      } else if (Bits <= 32) {
        TypeEncoded = 5;
      } else {
        TypeEncoded = 6;
      }
    }
      break;

    case Type::TypeID::HalfTyID:
      TypeEncoded = 7;
      break;
    case Type::TypeID::FloatTyID:
      TypeEncoded = 8;
      break;
    case Type::TypeID::DoubleTyID:
      TypeEncoded = 9;
      break;

    case Type::TypeID::X86_FP80TyID:
    case Type::TypeID::FP128TyID:
    case Type::TypeID::PPC_FP128TyID:
      TypeEncoded = 10;
      break;

    case Type::TypeID::PointerTyID:
      if (recurse) {
        TypeEncoded = uint16_t(16) + encodeType(dyn_cast<PointerType>(T)->getElementType(), false);
      } else {
        TypeEncoded = 11;
      }
      break;
    case Type::TypeID::StructTyID:
      TypeEncoded = 12;
      break;
    case Type::TypeID::ArrayTyID:
      TypeEncoded = 13;
      break;
    default:
      TypeEncoded = 14;
      break;
  }
  assert(TypeEncoded < 32);
  return TypeEncoded;
}
class SDAnalysis : public ModulePass {
public:
  static char ID;
  typedef std::pair<std::string, uint64_t> PreciseFunctionSignature;

  SDAnalysis() : ModulePass(ID) {
    sdLog::stream() << "initializing SDReturnAddress pass ...\n";
    initializeSDAnalysisPass(*PassRegistry::getPassRegistry());
  }

  virtual ~SDAnalysis() {
    sdLog::stream() << "deleting SDReturnAddress pass\n";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SDBuildCHA>();
    AU.addPreserved<SDBuildCHA>();
  }

private:

  struct VirtualCallsiteInfo {
    VirtualCallsiteInfo(std::string _FunctionName, std::string _Dwarf, int _Params) :
            FunctionName(_FunctionName), Dwarf(_Dwarf) {
      Params = _Params;
    }

  public:
    std::string FunctionName;
    std::string Dwarf;
    int Params;
    int SubhierarchyCount;
    int PreciseSubhierarchyCount;
    int TargetSignatureMatches;
    int ShortTargetSignatureMatches;
    int PreciseTargetSignatureMatches;
    int NumberOfEntries;
    int NumberOfParamMatches;

  };

  struct Encodings {
    Encodings() {}

    Encodings(uint64_t _Normal, uint64_t _Short, uint64_t _Precise) {
      Normal = _Normal;
      Short = _Short;
      Precise = _Precise;
    }
  public:
    uint64_t Normal;
    uint64_t Short;
    uint64_t Precise;
  };

  SDBuildCHA *CHA;

  std::map<uint64_t, int> FunctionWithEncodingCount;
  std::map<uint64_t, int> FunctionWithEncodingShortCount;
  std::map<std::string, Encodings> FunctionToEncoding;
  int NumberOfParameters[7];
  std::map<PreciseFunctionSignature, int> PreciseFunctionSignatureCount;

  std::vector<VirtualCallsiteInfo> Data;

  uint64_t encodeFunction(FunctionType *FuncTy, bool encodePointers, bool encodeReturnType = true) {
    uint64_t Encoding = 32;

    if (FuncTy->getNumParams() < 8) {
      if (encodeReturnType)
        Encoding = encodeType(FuncTy->getReturnType(), encodePointers);

      for (auto *Param : FuncTy->params()) {
        Encoding = encodeType(Param) + Encoding * 32;
      }
    }

    return Encoding;
  }

  bool runOnModule(Module &M) override {
    sdLog::blankLine();
    sdLog::stream() << "P7a. Started running the SDAnalysis pass ..." << sdLog::newLine << "\n";

    CHA = &getAnalysis<SDBuildCHA>();
    // Build the virtual ID ranges.
    CHA->buildFunctionInfo();

    // Process Callsites and annotate them for the backend pass.
    processVirtualFunctions(M);

    processVirtualCallSites(M);

    storeData(M);

    sdLog::stream() << sdLog::newLine << "P7a. Finished running the SDAnalysis pass ..." << "\n";
    sdLog::blankLine();
    return false;
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

  void processVirtualFunctions(Module &M) {
    for (auto &F : M) {
      if (!isVirtualFunction(F))
        continue;

      auto Encoding = encodeFunction(F.getFunctionType(), true);
      auto EncodingShort = encodeFunction(F.getFunctionType(), false);
      auto EncodingPrecise = encodeFunction(F.getFunctionType(), true, true);
      auto ParentFunctionName = CHA->getFunctionRootParent(F.getName());

      FunctionToEncoding[F.getName()] = Encodings(Encoding, EncodingShort, EncodingPrecise);

      for(int i = 0; i <= F.getFunctionType()->getNumParams(); ++i) {
        NumberOfParameters[i]++;
      }
      FunctionWithEncodingCount[Encoding]++;
      FunctionWithEncodingShortCount[EncodingShort]++;
      PreciseFunctionSignatureCount[PreciseFunctionSignature(ParentFunctionName,EncodingPrecise)]++;
    }
  }

  void processVirtualCallSites(Module &M) {
    Function *IntrinsicFunction = M.getFunction(Intrinsic::getName(Intrinsic::sd_get_checked_vptr));

    if (IntrinsicFunction == nullptr) {
      sdLog::warn() << "Intrinsic not found.\n";
      return;
    }

    sdLog::stream() << "\n";
    sdLog::stream() << "Processing virtual CallSites...\n";
    int count = 0;
    for (const Use &U : IntrinsicFunction->uses()) {

      // get the intrinsic call instruction
      CallInst *IntrinsicCall = dyn_cast<CallInst>(U.getUser());
      assert(IntrinsicCall && "Intrinsic was not wrapped in a CallInst?");

      // Find the CallSite that is associated with the intrinsic call.
      User *User = *(IntrinsicCall->users().begin());
      for (int i = 0; i < 3; ++i) {
        // User was not found, this should not happen...
        if (User == nullptr)
          break;
        errs() << User << "\n";

        for (auto *NextUser : User->users()) {
          User = NextUser;
          break;
        }
      }

      CallSite CallSite(User);
      if (CallSite.getInstruction()) {
        // valid CallSite
        processVirtualCall(IntrinsicCall, CallSite, M);
      } else {
        sdLog::log() << "\n";
        sdLog::warn() << "CallSite for intrinsic was not found.\n";
        IntrinsicCall->getParent()->dump();
      }
      ++count;
      sdLog::log() << "\n";
    }
    sdLog::stream() << "Found virtual CallSites: " << count << "\n";
  }

  void processVirtualCall(const CallInst *IntrinsicCall, CallSite CallSite, Module &M) {
    // Extract Metadata from Intrinsic.
    MetadataAsValue *Arg2 = dyn_cast<MetadataAsValue>(IntrinsicCall->getArgOperand(1));
    assert(Arg2);
    MDNode *ClassNameNode = dyn_cast<MDNode>(Arg2->getMetadata());
    assert(ClassNameNode);

    MetadataAsValue *Arg3 = dyn_cast<MetadataAsValue>(IntrinsicCall->getArgOperand(2));
    assert(Arg3);
    MDNode *PreciseNameNode = dyn_cast<MDNode>(Arg2->getMetadata());
    assert(PreciseNameNode);

    MetadataAsValue *Arg4 = dyn_cast<MetadataAsValue>(IntrinsicCall->getArgOperand(3));
    assert(Arg4);
    MDNode *FunctionNameNode = dyn_cast<MDNode>(Arg4->getMetadata());
    assert(FunctionNameNode);

    const StringRef ClassName = sd_getClassNameFromMD(ClassNameNode);
    const StringRef PreciseName = sd_getClassNameFromMD(PreciseNameNode);
    const StringRef FunctionName = sd_getFunctionNameFromMD(FunctionNameNode);


    const DebugLoc &Loc = CallSite.getInstruction()->getDebugLoc();
    std::string Dwarf = "";
    if (Loc) {
      std::stringstream Stream = writeDebugLocToStream(&Loc);
      Dwarf = Stream.str();
    }

    VirtualCallsiteInfo CallSiteInfo(FunctionName, Dwarf, CallSite.getFunctionType()->getNumParams());

    //sub-hierarchy
    CallSiteInfo.SubhierarchyCount = CHA->getCloudSize(ClassName);
    CallSiteInfo.PreciseSubhierarchyCount = CHA->getCloudSize(PreciseName);

    //src types, safe src types
    auto EncodingsPtr = FunctionToEncoding.find(FunctionName);
    if (EncodingsPtr == FunctionToEncoding.end())
      return;

    Encodings Encodings = EncodingsPtr->second;
    CallSiteInfo.TargetSignatureMatches = FunctionWithEncodingCount[Encodings.Normal];
    CallSiteInfo.ShortTargetSignatureMatches = FunctionWithEncodingShortCount[Encodings.Short];
    auto ParentFunctionName = CHA->getFunctionRootParent(FunctionName);
    CallSiteInfo.PreciseTargetSignatureMatches = PreciseFunctionSignatureCount[PreciseFunctionSignature(ParentFunctionName,Encodings.Precise)];

    CallSiteInfo.NumberOfEntries = CHA->getEntriesForFunction(FunctionName);
    auto NumberOfParam = CallSite.getFunctionType()->getNumParams();
    if (NumberOfParam > 6)
      NumberOfParam = 6;

    CallSiteInfo.NumberOfParamMatches = NumberOfParameters[NumberOfParam];
    Data.push_back(CallSiteInfo);
  }

  void storeData(Module &M) {
    sdLog::stream() << "Store all CallSites for Module: " << M.getName() << "\n";

    // Find new backup number
    int number = 0;
    auto outName = "./SD_CallSiteVirtualData-backup" + std::to_string(number);
    std::ifstream infile(outName);
    while (infile.good()) {
      number++;
      outName = "./SD_CallSiteVirtualData-backup" + std::to_string(number);
      infile = std::ifstream(outName);
    }

    // Virtual
    {
      std::ofstream Outfile("./SD_CallSiteVirtualData");
      for (auto Callsite : Data) {
        Outfile << Callsite.FunctionName
                << "," << Callsite.Dwarf
                << "," << Callsite.Params
                << "," << Callsite.NumberOfEntries
                << "," << Callsite.SubhierarchyCount
                << "," << Callsite.PreciseSubhierarchyCount
                << "," << Callsite.ShortTargetSignatureMatches
                << "," << Callsite.TargetSignatureMatches
                << "," << Callsite.PreciseTargetSignatureMatches
                << "," << Callsite.NumberOfParamMatches
                << "\n";
      }
      Outfile.close();

      // Write backup
      std::ifstream src("./SD_CallSiteVirtualData-backup", std::ios::binary);
      std::ofstream dst(outName, std::ios::binary);
      dst << src.rdbuf();
    }
  }
};

char SDAnalysis::ID = 0;

INITIALIZE_PASS(SDAnalysis, "sdAnalysis", "Build return ranges", false, false)

ModulePass *llvm::createSDAnalysisPass() {
  return new SDAnalysis();
}