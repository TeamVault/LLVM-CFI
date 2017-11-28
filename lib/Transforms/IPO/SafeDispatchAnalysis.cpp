//===-- SafeDispatchAnalysis.cpp - SafeDispatch Analysis code ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SDAnalysis class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Transforms/IPO/SafeDispatchLayoutBuilder.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <fstream>
#include <sstream>


using namespace llvm;

static const std::string itaniumConstructorTokens[3] = {"C0Ev", "C1Ev", "C2Ev"};

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

static uint64_t encodeFunction(FunctionType *FuncTy, bool encodePointers, bool encodeReturnType = true) {
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

static bool isBlackListed(const Function &F) {
  return (F.getName().startswith("llvm.") || F.getName().startswith("__")  || F.getName() == "_Znwm");
}

static bool isVirtualFunction(const Function &F) {
  if (!F.getName().startswith("_Z")) {
    return false;
  }

  for (auto &Token : itaniumConstructorTokens) {
    if (F.getName().endswith(Token)) {
      return false;
    }
  }

  return true;
}


class SDAnalysis : public ModulePass {
public:
  static char ID;

  SDAnalysis() : ModulePass(ID) {
    sdLog::stream() << "initializing SDAnalysis pass ...\n";
    initializeSDAnalysisPass(*PassRegistry::getPassRegistry());
  }

  ~SDAnalysis() override {
    sdLog::stream() << "deleting SDAnalysis pass\n";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SDBuildCHA>();
    AU.addPreserved<SDBuildCHA>();
  }

private:

  struct CallSiteInfo {
  public:
    explicit CallSiteInfo(const int _Params, const bool _isVirtual = false) :
            Params(_Params),
            isVirtual(_isVirtual) {}

    CallSiteInfo(const std::string &_FunctionName,
                 const std::string &_ClassName,
                 const std::string &_PreciseName,
                 int _Params) : CallSiteInfo(_Params, true) {
      FunctionName = _FunctionName;
      PreciseName = _PreciseName;
      ClassName = _ClassName;
    }


    const bool isVirtual;
    const int Params;
    std::string FunctionName = "";
    std::string ClassName = "";
    std::string PreciseName = "";

    std::string Dwarf = "";

    int64_t TargetSignatureMatches = -1;
    int64_t ShortTargetSignatureMatches = -1;
    int64_t PreciseTargetSignatureMatches= -1;
    int64_t NumberOfParamMatches = -1;

    int64_t TargetSignatureMatches_virtual = -1;
    int64_t ShortTargetSignatureMatches_virtual = -1;
    int64_t PreciseTargetSignatureMatches_virtual = -1;
    int64_t NumberOfParamMatches_virtual = -1;

    int64_t SubhierarchyCount = -1;
    int64_t PreciseSubhierarchyCount = -1;
    int64_t SizeOfVTableIsland = -1;
  };

  SDBuildCHA *CHA{};
  std::set<CallSite> VirtualCallSites{};
  std::vector<CallSiteInfo> Data{};

  typedef std::set<SDBuildCHA::func_name_t> func_name_set;

  int64_t count = 0;
  func_name_set allFunctions{};

  /** Start hierarchy analysis data */
  typedef std::map<uint64_t, SDBuildCHA::func_name_t> offset_to_func_name;
  typedef std::map<uint64_t, std::set<SDBuildCHA::func_name_t>> offset_to_func_name_set;

  std::map<SDBuildCHA::vtbl_t, offset_to_func_name> FunctionNameInVTableAtOffset{};
  std::map<SDBuildCHA::vtbl_name_t, offset_to_func_name_set> FunctionNamesInClassAtOffset{};

  std::map<SDBuildCHA::vtbl_t, std::set<SDBuildCHA::vtbl_t>> VTableSubHierarchy{};
  std::map<SDBuildCHA::vtbl_name_t, std::set<SDBuildCHA::vtbl_name_t>> ClassSubHierarchy{};

  std::map<SDBuildCHA::func_and_class_t, func_name_set> VTableSubHierarchyPerFunction{};
  std::map<SDBuildCHA::func_and_class_t, func_name_set> ClassSubHierarchyPerFunction{};
  /** End hierarchy analysis data */

  /** Start binary vtable tool data */
  int64_t NumberOfFunctionsWithVTablesEntry = -1;
  std::map<SDBuildCHA::func_and_class_t, func_name_set> ClassToIsland{};
  /** End binary vtable tool data */

  /** Start function type matching data */
  struct Encodings {
    Encodings() = default;

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

  typedef std::pair<std::string, uint64_t> PreciseFunctionSignature;

  std::map<PreciseFunctionSignature, int64_t> PreciseFunctionSignatureCount{};
  std::map<uint64_t, int64_t> FunctionWithEncodingCount{};
  std::map<uint64_t, int64_t> FunctionWithEncodingShortCount{};
  std::array<int64_t, 8> NumberOfParameters{};

  std::map<PreciseFunctionSignature, int64_t> PreciseFunctionSignatureCount_virtual{};
  std::map<uint64_t, int64_t> FunctionWithEncodingCount_virtual{};
  std::map<uint64_t, int64_t> FunctionWithEncodingShortCount_virtual{};
  std::array<int64_t, 8> NumberOfParameters_virtual{};
  /** End function type matching data */

  bool runOnModule(Module &M) override {
    sdLog::blankLine();
    sdLog::stream() << "P7a. Started running the SDAnalysis pass ..." << sdLog::newLine << "\n";

    CHA = &getAnalysis<SDBuildCHA>();
    CHA->buildFunctionInfo();

    analyseCHA();
    computeVTableIslands();
    countAllFunctionsWithVTableEntry();

    processFunctions(M);

    processVirtualCallSites(M);
    processIndirectCallSites(M);
    sdLog::stream() << "Found a total of " << count << "CallSites.\n";
    storeData(M);

    sdLog::stream() << sdLog::newLine << "P7a. Finished running the SDAnalysis pass ..." << "\n";
    sdLog::blankLine();
    return false;
  }

  /** Start hierarchy analysis functions */

  void analyseCHA() {
    sdLog::stream() << "Building hierarchies...\n";
    buildSubHierarchies();
    sdLog::stream() << "Computing targets in VTable hierarchy...\n";
    matchTargetsInVTableHierarchy();
    sdLog::stream() << "Computing targets in Class hierarchy...\n";
    matchTargetsInClassHierarchy();
    sdLog::stream() << "Finished hierarchy analysis!\n";

    sdLog::log() << "VTable hierarchy:\n";
    for (auto &entry : VTableSubHierarchyPerFunction) {
      sdLog::log() << entry.first.second << ", " << entry.first.first << ":";
      for (auto name : entry.second) {
        sdLog::logNoToken() << " " << name;
      }
      sdLog::logNoToken() << "\n";
    }

    sdLog::log() << "Class hierarchy:\n";
    for (auto &entry : ClassSubHierarchyPerFunction) {
      sdLog::log() << entry.first.second << ", " << entry.first.first << ":";
      for (auto name : entry.second) {
        sdLog::log() << " " << name;
      }
      sdLog::log() << "\n";
    }
  }

  void buildSubHierarchies() {
    auto topologicalOrder = CHA->topoSort();
    for (auto className = topologicalOrder.rbegin(); className != topologicalOrder.rend(); ++className) {
      auto vTableList = CHA->getSubVTables(*className);
      sdLog::log() << "\t" << *className << " with " << vTableList.size() << " vTables:\n";

      std::set<SDBuildCHA::vtbl_name_t> classChildren;
      offset_to_func_name_set functionNamesAtOffset;
      int vTableIndex = 0;
      for (auto &vTableType : vTableList) {
        auto vTable = SDBuildCHA::vtbl_t(*className, vTableIndex);
        sdLog::log() << "\t(" << vTable.first << ", " << vTable.second << ") of type " << vTableType << "\n";

        for (auto functionEntry : CHA->getFunctionEntries(vTable)) {
          sdLog::log() << "\t\t" << functionEntry.functionName << "@" << functionEntry.offsetInVTable << "\n";
          FunctionNameInVTableAtOffset[vTable][functionEntry.offsetInVTable] = functionEntry.functionName;
          FunctionNamesInClassAtOffset[vTable.first][functionEntry.offsetInVTable].insert(functionEntry.functionName);
        }

        std::set<SDBuildCHA::vtbl_t> vTableChildren;
        if (CHA->isDefined(vTable)) {
          vTableChildren.insert(vTable);
        }

        for (auto child = CHA->children_begin(vTable); child != CHA->children_end(vTable); child++) {
          assert(VTableSubHierarchy.find(*child) != VTableSubHierarchy.end());
          vTableChildren.insert(VTableSubHierarchy[*child].begin(), VTableSubHierarchy[*child].end());
        }

        VTableSubHierarchy[vTable] = vTableChildren;
        vTableIndex++;

        for (auto &vTable : vTableChildren) {
          classChildren.insert(vTable.first);
        }
      }

      ClassSubHierarchy[*className] = classChildren;
    }
  }

  void matchTargetsInVTableHierarchy() {
    for (auto &subHierarchy : VTableSubHierarchy) {
      auto rootVTable = subHierarchy.first;
      for (auto &functionNameEntry : FunctionNameInVTableAtOffset[rootVTable]) {
        auto offsetInVTable = functionNameEntry.first;

        std::set<SDBuildCHA::func_name_t> functionNames;
        for (auto &vTable : subHierarchy.second) {
          if (CHA->isDefined(vTable.first)) {
            functionNames.insert(FunctionNameInVTableAtOffset[vTable][offsetInVTable]);
          }
        }
        VTableSubHierarchyPerFunction[SDBuildCHA::func_and_class_t(functionNameEntry.second, rootVTable.first)]
                = functionNames;
      }
    }
  }

  void matchTargetsInClassHierarchy() {
    for (auto &subHierarchy : ClassSubHierarchy) {
      auto rootClassName = subHierarchy.first;
      if (FunctionNamesInClassAtOffset.find(rootClassName) == FunctionNamesInClassAtOffset.end()) {
        std::cerr << "WAT" << std::endl;
        continue;
      }

      for (auto &functionNameEntry : FunctionNamesInClassAtOffset[rootClassName]) {
        auto offsetInVTable = functionNameEntry.first;

        std::set<SDBuildCHA::func_name_t> functionNames;
        for (auto &className : subHierarchy.second) {
          if (CHA->isDefined(className)) {
            auto &functionNamesInChild  = FunctionNamesInClassAtOffset[className][offsetInVTable];
            functionNames.insert(functionNamesInChild.begin(), functionNamesInChild.end());
          }
        }
        for (auto &functionName : functionNameEntry.second) {
          ClassSubHierarchyPerFunction[SDBuildCHA::func_and_class_t(functionName, rootClassName)]
                  = functionNames;
        }
      }
    }
  }

  /** End hierarchy analysis functions */

  void countAllFunctionsWithVTableEntry() {
    std::set<SDBuildCHA::vtbl_name_t> functionNames;
    for (auto &classEntries : FunctionNamesInClassAtOffset) {
      if (CHA->isDefined(classEntries.first)) {
        for (auto &functionEntries : classEntries.second) {
          functionNames.insert(functionEntries.second.begin(), functionEntries.second.end());
        }
      }
    }

    NumberOfFunctionsWithVTablesEntry = functionNames.size();
    sdLog::stream() << "Number of Functions: " << NumberOfFunctionsWithVTablesEntry << "\n";
  }

  void computeVTableIslands() {
    std::map<SDBuildCHA::vtbl_name_t, std::set<SDBuildCHA::vtbl_name_t>> islands;
    std::map<SDBuildCHA::vtbl_name_t, SDBuildCHA::vtbl_name_t> classToIslandRoot;

    for (auto itr = CHA->roots_begin(); itr != CHA->roots_end(); ++itr) {
      auto root = SDBuildCHA::vtbl_t(*itr, 0);
      std::set<SDBuildCHA::vtbl_name_t> island;
      bool isNewIsland = true;
      auto islandRoot = root.first;

      for (auto &vTable : CHA->preorder(root)) {
        if (classToIslandRoot.find(vTable.first) == classToIslandRoot.end()) {
          classToIslandRoot[vTable.first] = islandRoot;
          island.insert(vTable.first);
        } else if (isNewIsland) {
          islandRoot = classToIslandRoot[vTable.first];
          isNewIsland = false;
          for (auto &entry : island) {
            classToIslandRoot[entry] = islandRoot;
          }
        }
      }
      islands[islandRoot].insert(island.begin(), island.end());
    }

    std::map<SDBuildCHA::vtbl_name_t, offset_to_func_name_set> islandToFunctionsAtOffset;
    for(auto &island : islands) {
      for (auto &className : island.second) {
        if (CHA->isDefined(className)) {
          for (auto &entry : FunctionNamesInClassAtOffset[className]) {
            islandToFunctionsAtOffset[island.first][entry.first].insert(entry.second.begin(), entry.second.end());
          }
        }
      }
    }

    for(auto &entry : classToIslandRoot) {
      for (auto &functionNameEntry : FunctionNamesInClassAtOffset[entry.first]) {
        auto offsetInVTable = functionNameEntry.first;
        for (auto &functionName : functionNameEntry.second) {
          ClassToIsland[{functionName, entry.first}] = islandToFunctionsAtOffset[entry.second][offsetInVTable];
        }
      }
    }
    sdLog::stream() << "Number of islands: " << islands.size() << "\n";
  }

  /** Start function type matching functions */

  void processFunctions(Module &M) {
    sdLog::stream() << "\n";
    sdLog::stream() << "Processing functions...\n";

    for (auto &F : M) {
      if (isBlackListed(F))
        continue;

      allFunctions.insert(F.getName());

      auto NumOfParams = F.getFunctionType()->getNumParams();
      if (NumOfParams > 7)
        NumOfParams = 7;

      auto Encode = createEncoding(F.getFunctionType());

      NumberOfParameters[NumOfParams]++;
      FunctionWithEncodingCount[Encode.Normal]++;
      FunctionWithEncodingShortCount[Encode.Short]++;

      int Status = 0;
      char *Demangled =
              itaniumDemangle(F.getName().str().c_str(), nullptr, nullptr, &Status);
      if (Demangled != nullptr && Status == 0) {
        errs() << Demangled;
      }

      if (Demangled != nullptr)
        std::free(Demangled);

      if (isVirtualFunction(F)) {
        auto ParentFunctionName = CHA->getFunctionRootParent(F.getName());

        NumberOfParameters_virtual[NumOfParams]++;
        FunctionWithEncodingCount_virtual[Encode.Normal]++;
        FunctionWithEncodingShortCount_virtual[Encode.Short]++;
        PreciseFunctionSignatureCount_virtual[PreciseFunctionSignature(ParentFunctionName, Encode.Precise)]++;
      }
    }

    sdLog::stream() << "\n";
    for (int i = 0; i < NumberOfParameters.size() - 1; ++i) {
      sdLog::stream() << "Number of functions with " << i << " params: ("
                      << NumberOfParameters[i] << "," << NumberOfParameters_virtual[i] << ")\n";
    }
    sdLog::stream() << "Number of functions with 7+ params: "
                    << NumberOfParameters[7] << "," << NumberOfParameters_virtual[7] << ")\n";

  }

  /** End function type matching functions */

  /** Start CallSite analysis functions */

  void processIndirectCallSites(Module &M) {
    int64_t countIndirect = 0;

    sdLog::stream() << "\n";
    sdLog::stream() << "Processing indirect CallSites...\n";
    for (auto &F : M) {
      for(auto &MBB : F) {
        for (auto &I : MBB) {
          CallSite Call(&I);
          // Try to use I as a CallInst or a InvokeInst
          if (Call.getInstruction()) {
            if (CallSite(Call).isIndirectCall() && VirtualCallSites.find(Call) == VirtualCallSites.end()) {
              CallSiteInfo Info(Call.getFunctionType()->getNumParams(), false);
              analyseCall(Call, Info);
              ++countIndirect;
            }
          }
        }
      }
    }
    sdLog::stream() << "Found indirect CallSites: " << countIndirect << "\n";
    sdLog::stream() << "\n";
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
      CallSite *VCall = nullptr;
      for (int i = 0; i < 4; ++i) {
        // User was not found, this should not happen...
        VCall = new CallSite(User);
        if (VCall->getInstruction()) {
          break;
        }

        for (auto *NextUser : User->users()) {
          User = NextUser;
          break;
        }
      }

      if (VCall != nullptr && VCall->getInstruction()) {
        // valid CallSite
        extractVirtualCallSiteInfo(IntrinsicCall, *VCall);
        VirtualCallSites.insert(*VCall);
      } else {
        sdLog::warn() << "CallSite for intrinsic was not found.\n";
        IntrinsicCall->getParent()->dump();
      }
      ++count;
    }
    sdLog::stream() << "Found virtual CallSites: " << count << "\n";
  }

  void extractVirtualCallSiteInfo(const CallInst *IntrinsicCall, CallSite CallSite) {
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

    CallSiteInfo Info(FunctionName, ClassName, PreciseName, CallSite.getFunctionType()->getNumParams());
    analyseCall(CallSite, Info);
  }

  void analyseCall(CallSite CallSite, CallSiteInfo Info) {
    count++;
    const DebugLoc &Loc = CallSite.getInstruction()->getDebugLoc();
    std::string Dwarf;
    if (Loc) {
      std::stringstream Stream = writeDebugLocToStream(&Loc);
      Dwarf = Stream.str();
    }
    Info.Dwarf = Dwarf;

    auto NumberOfParam = CallSite.getFunctionType()->getNumParams();
    if (NumberOfParam >= 7)
      NumberOfParam = 7;

    auto Encode = createEncoding(CallSite.getFunctionType());

    Info.TargetSignatureMatches = FunctionWithEncodingCount[Encode.Normal];
    Info.ShortTargetSignatureMatches = FunctionWithEncodingShortCount[Encode.Short];
    Info.NumberOfParamMatches = 0;
    for (int i = 0; i <= NumberOfParam; ++i) {
      Info.NumberOfParamMatches += NumberOfParameters[i];
    }

    Info.TargetSignatureMatches_virtual = FunctionWithEncodingCount_virtual[Encode.Normal];
    Info.ShortTargetSignatureMatches_virtual = FunctionWithEncodingShortCount_virtual[Encode.Short];
    Info.NumberOfParamMatches_virtual = 0;
    for (int i = 0; i <= NumberOfParam; ++i) {
      Info.NumberOfParamMatches_virtual += NumberOfParameters_virtual[i];
    }

    if (Info.isVirtual) {
      auto func_and_class = SDBuildCHA::func_and_class_t(Info.FunctionName, Info.PreciseName);

      Info.SubhierarchyCount = ClassSubHierarchyPerFunction[func_and_class].size();
      Info.PreciseSubhierarchyCount = VTableSubHierarchyPerFunction[func_and_class].size();
      Info.SizeOfVTableIsland = ClassToIsland[func_and_class].size();

      int Status = 0;
      char *Demangled =
              itaniumDemangle(Info.FunctionName.c_str(), nullptr, nullptr, &Status);
      if (Demangled != nullptr && Status == 0) {
        errs() << Demangled;
      }

      if (Demangled != nullptr)
        std::free(Demangled);


      auto ParentFunctionName = CHA->getFunctionRootParent(Info.FunctionName);
      if (ParentFunctionName != "") {
        Info.PreciseTargetSignatureMatches_virtual =
                PreciseFunctionSignatureCount[PreciseFunctionSignature(ParentFunctionName,Encode.Precise)];
      }
    }
    Data.push_back(Info);
  }

  /** End CallSite analysis functions */

  void storeData(Module &M) {
    sdLog::stream() << "Store all CallSites for Module: " << M.getName() << "\n";

    auto SDOutputMD = M.getNamedMetadata("sd_output");
    auto SDFilenameMD = M.getNamedMetadata("sd_filename");

    StringRef OutputPath;
    if (SDOutputMD != nullptr)
      OutputPath = dyn_cast_or_null<MDString>(SDOutputMD->getOperand(0)->getOperand(0))->getString();
    else if (SDFilenameMD != nullptr)
      OutputPath = ("./" + dyn_cast_or_null<MDString>(SDOutputMD->getOperand(0)->getOperand(0))->getString()).str();

    std::string OutfileVirtualName = "./SDAnalysis-Virtual";
    std::string OutfileIndirectName = "./SDAnalysis-Indirect";
    if (OutputPath != "") {
      OutfileVirtualName = (OutputPath + "-Virtual").str();
      OutfileIndirectName = (OutputPath + "-Indirect").str();
    }

    std::string VirtualFileName = (Twine(OutfileVirtualName) + ".csv").str();
    std::string IndirectFileName = (Twine(OutfileIndirectName) + ".csv").str();
    if (sys::fs::exists(OutfileVirtualName + ".csv") || sys::fs::exists(OutfileVirtualName + ".csv")) {
      uint number = 1;
      while (sys::fs::exists(OutfileVirtualName + Twine(number) + ".csv")
             || sys::fs::exists(OutfileVirtualName + Twine(number) + ".csv")) {
        number++;
      }
      VirtualFileName = (OutfileVirtualName + Twine(number) + ".csv").str();
      IndirectFileName = (OutfileIndirectName + Twine(number) + ".csv").str();
    }

    std::error_code ECVirtual, ECIndirect;
    raw_fd_ostream OutfileVirtual(VirtualFileName, ECVirtual, sys::fs::OpenFlags::F_None);
    raw_fd_ostream OutfileIndirect(IndirectFileName, ECIndirect, sys::fs::OpenFlags::F_None);

    if (ECVirtual || ECIndirect) {
      sdLog::errs() << "Failed to write to " << VirtualFileName << ", " << IndirectFileName << "\n";
      return;
    }

    sdLog::stream() << "Writing " << Data.size() << " lines to " << VirtualFileName << ", " << VirtualFileName << "\n";

    std::stringstream ShortHeader;
    ShortHeader << "Dwarf"
                << ",FunctionName"
                << ",ClassName"
                << ",PreciseName"
                << ",Params"
                << ","
                << ",PreciseSrcType"
                << ",SrcType"
                << ",SafeSrcType"
                << ",BinType"
                << ","
                << ",PreciseSrcType-VFunctions"
                << ",SrcType-VFunctions"
                << ",SafeSrcType-VFunctions"
                << ",BinType-VFunctions";

    std::stringstream FullHeader;
    FullHeader << ShortHeader.str()
               << ","
               << ",VTableSubHierarchy"
               << ",ClassSubHierarchy"
               << ",VTableIsland"
               << ",All VTables"
               << "\n";

    ShortHeader << "\n";

    OutfileVirtual << FullHeader.str();
    OutfileIndirect << ShortHeader.str();
    for (auto &Info : Data) {
      if (Info.isVirtual) {
        OutfileVirtual
                << Info.Dwarf
                << "," << Info.FunctionName
                << "," << Info.ClassName
                << "," << Info.PreciseName
                << "," << Info.Params
                << ","
                << "," << Info.PreciseTargetSignatureMatches
                << "," << Info.TargetSignatureMatches
                << "," << Info.ShortTargetSignatureMatches
                << "," << Info.NumberOfParamMatches
                << ","
                << "," << Info.PreciseTargetSignatureMatches_virtual
                << "," << Info.TargetSignatureMatches_virtual
                << "," << Info.ShortTargetSignatureMatches_virtual
                << "," << Info.NumberOfParamMatches_virtual
                << ","
                << "," << Info.PreciseSubhierarchyCount
                << "," << Info.SubhierarchyCount
                << "," << Info.SizeOfVTableIsland
                << "," << NumberOfFunctionsWithVTablesEntry
                << "\n";
      } else {
        OutfileIndirect
                << Info.Dwarf
                << ","
                << ","
                << ","
                << "," << Info.Params
                << ","
                << "," << Info.PreciseTargetSignatureMatches
                << "," << Info.TargetSignatureMatches
                << "," << Info.ShortTargetSignatureMatches
                << "," << Info.NumberOfParamMatches
                << ","
                << "," << Info.PreciseTargetSignatureMatches_virtual
                << "," << Info.TargetSignatureMatches_virtual
                << "," << Info.ShortTargetSignatureMatches_virtual
                << "," << Info.NumberOfParamMatches_virtual
                << "\n";

      };
    };

    OutfileVirtual.close();
    OutfileIndirect.close();
  }

  static Encodings createEncoding(FunctionType* Type) {
    auto Encoding = encodeFunction(Type, true);
    auto EncodingShort = encodeFunction(Type, false);
    auto EncodingPrecise = encodeFunction(Type, true, true);
    return {Encoding, EncodingShort, EncodingPrecise};
  }
};

char SDAnalysis::ID = 0;

INITIALIZE_PASS(SDAnalysis, "sdAnalysis", "Build return ranges", false, false)

ModulePass *llvm::createSDAnalysisPass() {
  return new SDAnalysis();
}