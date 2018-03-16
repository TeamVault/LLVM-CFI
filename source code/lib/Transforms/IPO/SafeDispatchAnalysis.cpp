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

/** Encodings contains the three relevant encodings */
struct Encodings {
public:
  Encodings() = default;

  Encodings(uint64_t _Normal, uint64_t _Short, uint64_t _Precise) {
    Normal = _Normal;
    Short = _Short;
    Precise = _Precise;
  }

  uint64_t Normal;
  uint64_t Short;
  uint64_t Precise;

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

  static Encodings encode(FunctionType* Type) {
    auto Encoding = encodeFunction(Type, true);
    auto EncodingShort = encodeFunction(Type, false);
    auto EncodingPrecise = encodeFunction(Type, true, true);
    return {Encoding, EncodingShort, EncodingPrecise};
  }
};

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

  /** CallSiteInfo encapsulates all analysis data for a single CallSite.
   *  Iff isVirtual == false:
   *    FunctionName, ClassName, PreciseName
   *    and SubHierarchyMatches, PreciseSubHierarchyMatches, HierarchyIslandMatches
   *    will not contain meaningful data.
   * */
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
    std::string FunctionName = "";
    std::string ClassName = "";
    std::string PreciseName = "";

    const int Params;
    std::string Dwarf = "";
    Encodings Encoding{};

    int64_t TargetSignatureMatches = -1;
    int64_t ShortTargetSignatureMatches = -1;
    int64_t PreciseTargetSignatureMatches= -1;
    int64_t NumberOfParamMatches = -1;

    int64_t TargetSignatureMatches_virtual = -1;
    int64_t ShortTargetSignatureMatches_virtual = -1;
    int64_t PreciseTargetSignatureMatches_virtual = -1;
    int64_t NumberOfParamMatches_virtual = -1;

    int64_t SubHierarchyMatches = -1;
    int64_t PreciseSubHierarchyMatches = -1;
    int64_t HierarchyIslandMatches = -1;
  };

  typedef std::set<SDBuildCHA::func_name_t> func_name_set;
  typedef std::map<uint64_t, SDBuildCHA::func_name_t> offset_to_func_name;
  typedef std::map<uint64_t, std::set<SDBuildCHA::func_name_t>> offset_to_func_name_set;
  typedef std::pair<std::string, uint64_t> preciseFunctionSignature_t;

  SDBuildCHA *CHA{};

  std::set<CallSite> VirtualCallSites{};  // analysed vcall (used to filter the remaining indirect calls)
  int64_t CallSiteCount = 0;              // counts analysed CallSites
  std::vector<CallSiteInfo> Data{};       // info for every analysed CallSite

  // metric results (used for sorting CallSiteInfo)
  std::map<float, std::vector<CallSiteInfo>> MetricVirtual{};
  std::map<float, std::vector<CallSiteInfo>> MetricIndirect{};

  func_name_set AllFunctions{};           // baseline
  func_name_set AllVFunctions{};          // baseline virtual functions

  /** hierarchy analysis data */

  // vTable hierarchy (ShrinkWrap / IVT)
  std::map<SDBuildCHA::vtbl_t, offset_to_func_name> FunctionNameInVTableAtOffset{};
  std::map<SDBuildCHA::vtbl_t, std::set<SDBuildCHA::vtbl_t>> VTableSubHierarchy{};
  std::map<SDBuildCHA::func_and_class_t, func_name_set> VTableSubHierarchyPerFunction{};

  // class hierarchy (VTV)
  std::map<SDBuildCHA::vtbl_name_t, offset_to_func_name_set> FunctionNamesInClassAtOffset{};
  std::map<SDBuildCHA::vtbl_name_t, std::set<SDBuildCHA::vtbl_name_t>> ClassSubHierarchy{};
  std::map<SDBuildCHA::func_and_class_t, func_name_set> ClassSubHierarchyPerFunction{};

  // class hierarchy islands (Marx)
  std::map<SDBuildCHA::func_and_class_t, func_name_set> ClassToIsland{};
  // all functions in any vTable (vTint)
  int64_t AllVFunctionsInVTables = 0;

  /** function type matching data */

  std::map<preciseFunctionSignature_t, func_name_set> PreciseTargetSignature{};
  std::map<uint64_t, func_name_set> TargetSignature{};
  std::map<uint64_t, func_name_set> ShortTargetSignature{};
  std::array<int64_t, 8> NumberOfParameters{};

  std::map<preciseFunctionSignature_t, func_name_set> PreciseTargetSignature_virtual{};
  std::map<uint64_t, func_name_set> TargetSignature_virtual{};
  std::map<uint64_t, func_name_set> ShortTargetSignature_virtual{};
  std::array<int64_t, 8> NumberOfParameters_virtual{};

  bool runOnModule(Module &M) override {
    sdLog::blankLine();
    sdLog::stream() << "P7a. Started running the SDAnalysis pass ..." << sdLog::newLine << "\n";

    // setup CHA info
    CHA = &getAnalysis<SDBuildCHA>();
    analyseCHA();
    computeVTableIslands();
    findAllVFunctions();

    // setup callee and callee signature info
    analyseCallees(M);

    // process the CallSites
    processVirtualCallSites(M);
    processIndirectCallSites(M);
    sdLog::stream() << "Total number of CallSites: " << CallSiteCount << "\n";

    // apply the metric to the CallSiteInfo's in order to sort them
    applyCallSiteMetric();
    // store the analysis data
    storeData(M);

    sdLog::stream() << sdLog::newLine << "P7a. Finished running the SDAnalysis pass ..." << "\n";
    sdLog::blankLine();
    return false;
  }

  /** hierarchy analysis functions */

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

  void findAllVFunctions() {
    for (auto &classEntries : FunctionNamesInClassAtOffset) {
      if (CHA->isDefined(classEntries.first)) {
        for (auto &functionEntries : classEntries.second) {
          AllVFunctions.insert(functionEntries.second.begin(), functionEntries.second.end());
        }
      }
    }
    AllVFunctionsInVTables = AllVFunctions.size();
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


  /** function type matching functions */

  void analyseCallees(Module &M) {
    sdLog::stream() << "\n";
    sdLog::stream() << "Processing functions...\n";

    for (auto &F : M) {
      if (isBlackListed(F))
        continue;

      auto NumOfParams = F.getFunctionType()->getNumParams();
      if (NumOfParams > 7)
        NumOfParams = 7;

      auto Encode = Encodings::encode(F.getFunctionType());
      std::string FunctionName = F.getName();

      std::string DemangledFunctionName = FunctionName;
      int Status = 0;
      if (F.getName().startswith("_")) {
        auto DemangledPair = itaniumDemanglePair(F.getName(), Status);
        if (Status == 0 && DemangledPair.second != "") {
          DemangledFunctionName = DemangledPair.second;
        }
      }

      AllFunctions.insert(F.getName());
      NumberOfParameters[NumOfParams]++;
      TargetSignature[Encode.Normal].insert(F.getName());
      ShortTargetSignature[Encode.Short].insert(F.getName());
      PreciseTargetSignature[preciseFunctionSignature_t(DemangledFunctionName, Encode.Precise)]
              .insert(FunctionName);

      if (isVirtualFunction(F)) {
        AllVFunctions.insert(F.getName());
        NumberOfParameters_virtual[NumOfParams]++;
        TargetSignature_virtual[Encode.Normal].insert(F.getName());
        ShortTargetSignature_virtual[Encode.Short].insert(F.getName());
        PreciseTargetSignature_virtual[preciseFunctionSignature_t(DemangledFunctionName, Encode.Precise)]
                .insert(FunctionName);
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

  /** CallSite analysis functions */

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
    CallSiteCount++;
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

    auto Encode = Encodings::encode(CallSite.getFunctionType());
    Info.Encoding = Encode;

    Info.TargetSignatureMatches = TargetSignature[Encode.Normal].size();
    Info.ShortTargetSignatureMatches = ShortTargetSignature[Encode.Short].size();

    Info.NumberOfParamMatches = 0;
    for (int i = 0; i <= NumberOfParam; ++i) {
      Info.NumberOfParamMatches += NumberOfParameters[i];
    }

    Info.TargetSignatureMatches_virtual = TargetSignature_virtual[Encode.Normal].size();
    Info.ShortTargetSignatureMatches_virtual = ShortTargetSignature_virtual[Encode.Short].size();

    Info.NumberOfParamMatches_virtual = 0;
    for (int i = 0; i <= NumberOfParam; ++i) {
      Info.NumberOfParamMatches_virtual += NumberOfParameters_virtual[i];
    }

    if (Info.isVirtual) {
      auto func_and_class = SDBuildCHA::func_and_class_t(Info.FunctionName, Info.PreciseName);

      Info.SubHierarchyMatches = ClassSubHierarchyPerFunction[func_and_class].size();
      Info.PreciseSubHierarchyMatches = VTableSubHierarchyPerFunction[func_and_class].size();
      Info.HierarchyIslandMatches = ClassToIsland[func_and_class].size();

      std::string DemangledFunctionName = Info.FunctionName;
      int Status = 0;
      auto DemangledPair = itaniumDemanglePair(Info.FunctionName, Status);
      if (Status == 0 && DemangledPair.second != "") {
        DemangledFunctionName = DemangledPair.second;
      }


      Info.PreciseTargetSignatureMatches =
              PreciseTargetSignature[preciseFunctionSignature_t(DemangledFunctionName,Encode.Precise)].size();

      Info.PreciseTargetSignatureMatches_virtual =
              PreciseTargetSignature_virtual[preciseFunctionSignature_t(DemangledFunctionName,Encode.Precise)].size();
    }

    Data.push_back(Info);
  }

  /** Helper functions */

  void applyCallSiteMetric() {
    for (auto& entry : Data) {
      if (entry.isVirtual) {
        float metric = entry.TargetSignatureMatches - entry.PreciseSubHierarchyMatches;
        if (entry.PreciseSubHierarchyMatches != 0) {
          metric /= entry.PreciseSubHierarchyMatches;
        }
        MetricVirtual[metric].push_back(entry);
      } else {
        float metric = entry.TargetSignatureMatches / (float) (AllFunctions.size());
        MetricIndirect[metric].push_back(entry);
      }
    }
  }

  void storeData(Module &M) {
    if (Data.empty()) {
      sdLog::stream() << "Nothing to store...\n";
      return;
    }
    sdLog::stream() << "Store all CallSites for Module: " << M.getName() << "\n";

    auto FileNames = findOutputFileName(M);

    // write general analysis data

    std::error_code ECVirtual, ECIndirect;
    raw_fd_ostream OutfileVirtual(FileNames.first, ECVirtual, sys::fs::OpenFlags::F_None);
    raw_fd_ostream OutfileIndirect(FileNames.second, ECIndirect, sys::fs::OpenFlags::F_None);
    if (ECVirtual || ECIndirect) {
      sdLog::errs() << "Failed to write to " << FileNames.first << ", " << FileNames.second << "!\n";
      return;
    }
    sdLog::stream() << "Writing " << Data.size() << " lines to "
                    << FileNames.first << ", " << FileNames.second << ".\n";

    writeAnalysisData(OutfileVirtual, OutfileIndirect);

    // write metric

    std::string MetricFileNameVirtual = FileNames.first.substr(0, FileNames.first.size() - 4) + "-metric.csv";
    std::string MetricFileNameIndirect = FileNames.second.substr(0, FileNames.second.size() - 4) + "-metric.csv";

    raw_fd_ostream OutfileMetricVirtual(MetricFileNameVirtual, ECVirtual, sys::fs::OpenFlags::F_None);
    raw_fd_ostream OutfileMetricIndirect(MetricFileNameIndirect, ECIndirect, sys::fs::OpenFlags::F_None);
    if (ECVirtual || ECIndirect) {
      sdLog::errs() << "Failed to write to " << MetricFileNameVirtual << ", " << MetricFileNameIndirect << "!\n";
      return;
    }
    sdLog::stream() << "Writing metric results to "
                    << MetricFileNameVirtual << ", " << MetricFileNameIndirect << ".\n";

    writeMetricVirtual(OutfileMetricVirtual);
    writeMetricIndirect(OutfileMetricIndirect);
  }

  void writeAnalysisData(raw_fd_ostream &OutfileVirtual, raw_fd_ostream &OutfileIndirect) {
    writeHeader(OutfileVirtual, true);
    writeHeader(OutfileIndirect, false);

    auto BaseLine = AllFunctions.size();
    auto BaseLineVirtual = AllVFunctions.size();
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
                << "," << BaseLine
                << ","
                << "," << Info.PreciseTargetSignatureMatches_virtual
                << "," << Info.TargetSignatureMatches_virtual
                << "," << Info.ShortTargetSignatureMatches_virtual
                << "," << Info.NumberOfParamMatches_virtual
                << "," << BaseLineVirtual
                << ","
                << "," << Info.PreciseSubHierarchyMatches
                << "," << Info.SubHierarchyMatches
                << "," << Info.HierarchyIslandMatches
                << "," << AllVFunctionsInVTables
                << "\n";
      } else {
        OutfileIndirect
                << Info.Dwarf
                << ","
                << ","
                << ","
                << "," << Info.Params
                << ","
                << ","
                << "," << Info.TargetSignatureMatches
                << "," << Info.ShortTargetSignatureMatches
                << "," << Info.NumberOfParamMatches
                << "," << AllFunctions.size()
                << ","
                << ","
                << "," << Info.TargetSignatureMatches_virtual
                << "," << Info.ShortTargetSignatureMatches_virtual
                << "," << Info.NumberOfParamMatches_virtual
                << "," << BaseLineVirtual
                << "\n";

      };
    };

    OutfileVirtual.close();
    OutfileIndirect.close();
  }

  void writeMetricVirtual(raw_fd_ostream &Out) {
    if (MetricVirtual.empty())
      return;

    writeHeader(Out, true);

    auto BaseLine = AllFunctions.size();
    auto BaseLineVirtual = AllVFunctions.size();
    std::set<std::string> ExportedLines;

    int i = 0;
    for (auto I = MetricVirtual.rbegin(), E = MetricVirtual.rend(); I != E; ++I) {
      for (auto &Info : I->second) {
        if (ExportedLines.find(Info.Dwarf) != ExportedLines.end())
          continue;

        ExportedLines.insert(Info.Dwarf);
        Out << Info.Dwarf
            << "," << Info.FunctionName
            << "," << Info.ClassName
            << "," << Info.PreciseName
            << "," << Info.Params
            << ","
            << "," << Info.PreciseTargetSignatureMatches
            << "," << Info.TargetSignatureMatches
            << "," << Info.ShortTargetSignatureMatches
            << "," << Info.NumberOfParamMatches
            << "," << BaseLine
            << ","
            << "," << Info.PreciseTargetSignatureMatches_virtual
            << "," << Info.TargetSignatureMatches_virtual
            << "," << Info.ShortTargetSignatureMatches_virtual
            << "," << Info.NumberOfParamMatches_virtual
            << "," << BaseLineVirtual
            << ","
            << "," << Info.PreciseSubHierarchyMatches
            << "," << Info.SubHierarchyMatches
            << "," << Info.HierarchyIslandMatches
            << "," << AllVFunctionsInVTables;

        std::set<std::string> ValidTargets, Targets, InvalidTargets;

        auto ValidItr = VTableSubHierarchyPerFunction.find(
                SDBuildCHA::func_and_class_t(Info.FunctionName, Info.PreciseName));
        if (ValidItr != VTableSubHierarchyPerFunction.end())
          ValidTargets = ValidItr->second;

        auto TargetItr = TargetSignature.find(Info.Encoding.Normal);
        if (TargetItr != TargetSignature.end())
          Targets = TargetItr->second;

        std::set_difference(Targets.begin(), Targets.end(),
                            ValidTargets.begin(), ValidTargets.end(),
                            std::inserter(InvalidTargets, InvalidTargets.end()));

        Out << ",ValidTargets (" << ValidTargets.size() << "):";
        for (const SDBuildCHA::func_name_t &Target : ValidTargets) {
          auto FunctionName = Target;
          if (StringRef(FunctionName).startswith("_Z")) {
            int Status = 0;
            auto DemangledPair = itaniumDemanglePair(Target, Status);
            if (Status == 0 && DemangledPair.first != "") {
              FunctionName = DemangledPair.first;
            }
          }
          Out << "," << FunctionName;
        }

        Out << ",InvalidTargets (" << InvalidTargets.size() << "):";
        for (const SDBuildCHA::func_name_t &Target : InvalidTargets) {
          auto FunctionName = Target;
          if (StringRef(FunctionName).startswith("_Z")) {
            int Status = 0;
            auto DemangledPair = itaniumDemanglePair(Target, Status);
            if (Status == 0 && DemangledPair.first != "") {
              FunctionName = DemangledPair.first;
            }
          }
          Out << "," << FunctionName;
        }

        Out << "\n";
        i++;
      }

      if (i > 30) {
        break;
      }
    }
    Out.close();

  }

  void writeMetricIndirect(raw_fd_ostream &Out) {
    if (MetricIndirect.empty())
      return;

    writeHeader(Out, false);

    auto BaseLine = AllFunctions.size();
    auto BaseLineVirtual = AllVFunctions.size();
    std::set<std::string> ExportedLines;

    int i = 0;
    for (auto I = MetricIndirect.rbegin(), E = MetricIndirect.rend(); I != E; ++I) {
      for (auto &Info : I->second) {
        if (ExportedLines.find(Info.Dwarf) != ExportedLines.end())
          continue;

        ExportedLines.insert(Info.Dwarf);
        Out << Info.Dwarf
            << ","
            << ","
            << ","
            << "," << Info.Params
            << ","
            << ","
            << "," << Info.TargetSignatureMatches
            << "," << Info.ShortTargetSignatureMatches
            << "," << Info.NumberOfParamMatches
            << "," << AllFunctions.size()
            << ","
            << ","
            << "," << Info.TargetSignatureMatches_virtual
            << "," << Info.ShortTargetSignatureMatches_virtual
            << "," << Info.NumberOfParamMatches_virtual
            << "," << BaseLineVirtual;

        std::set<std::string> Targets;
        auto TargetItr = TargetSignature.find(Info.Encoding.Normal);
        if (TargetItr != TargetSignature.end())
          Targets = TargetItr->second;

        Out << ",Targets (" << Targets.size() << "):";
        for (const SDBuildCHA::func_name_t &Target : Targets) {
          auto FunctionName = Target;
          if (StringRef(FunctionName).startswith("_Z")) {
            int Status = 0;
            auto DemangledPair = itaniumDemanglePair(Target, Status);
            if (Status == 0 && DemangledPair.first != "") {
              FunctionName = DemangledPair.first;
            }
          }
          Out << "," << FunctionName;
        }

        Out << "\n";
        i++;
      }

      if (i > 50) {
        break;
      }
    }
    Out.close();

  }

  void writeHeader(raw_ostream &Out, bool writeFullHeader, bool writeDetails = true) {
    std::stringstream ShortHeader, ShortDetails, FullHeader, FullDetails;

    Out << "Dwarf"
        << ",FunctionName"
        << ",ClassName"
        << ",PreciseName"
        << ",Params"
        << ","
        << ",PreciseSrcType (vTrust)"
        << ",SrcType (IFCC)"
        << ",SafeSrcType (IFCC-safe)"
        << ",BinType (TypeArmor)"
        << ",Baseline"
        << ","
        << ",PreciseSrcType-VFunctions"
        << ",SrcType-VFunctions"
        << ",SafeSrcType-VFunctions"
        << ",BinType-VFunctions"
        << ",Baseline-VFunctions";
    if (writeFullHeader) {
      Out << ","
          << ",VTableSubHierarchy (ShrinkWrap)"
          << ",ClassSubHierarchy (VTV)"
          << ",ClassIsland (Marx)"
          << ",AllVTables (vTint)";
    }
    Out << "\n";

    if (writeDetails) {
      Out << "(The dwarf info of this callsite)"
          << ",(The least-derived vfunction used at this vcall)"
          << ",(The class defining functionname)"
          << ",(The least-derived class of the object used at this vcall)"
          << ",(# of params provided by this callsite (=# consumed))"
          << ","
          << ",(func sig matching including C/C++ func name & ret type)"
          << ",(param type matching w/ pointer types)"
          << ",(param type matching wo/ pointer types)"
          << ",(Callsite param >= Callee param (up to 6)"
          << ",(total # of functions)"
          << ","
          << ",(PreciseSrcType only virtual targets)"
          << ",(SrcType only virtual targets)"
          << ",(SafeSrcType only virtual targets)"
          << ",(BinType only virtual targets)"
          << ",(total # of virtual targets)";

      if (writeFullHeader) {
        Out << ","
            << ",(targets at offset in vTable hierarchy with PreciseName as root)"
            << ",(targets at offset in class hierarchy with PreciseName as root)"
            << ",(targets in class hierarchy island containing PreciseName)"
            << ",(targets in any vtable)";
      }
      Out << "\n";
    }
  }

  std::pair<std::string, std::string> findOutputFileName(Module &M) {
    auto SDOutputMD = M.getNamedMetadata("sd_output");
    auto SDFilenameMD = M.getNamedMetadata("sd_filename");

    StringRef OutputPath;
    if (SDOutputMD != nullptr)
      OutputPath = dyn_cast_or_null<MDString>(SDOutputMD->getOperand(0)->getOperand(0))->getString();
    else if (SDFilenameMD != nullptr)
      OutputPath = ("./" + dyn_cast_or_null<MDString>(SDFilenameMD->getOperand(0)->getOperand(0))->getString()).str();

    std::string VirtualFileName = "./SDAnalysis-Virtual";
    std::string IndirectFileName = "./SDAnalysis-Indirect";
    if (OutputPath != "") {
      VirtualFileName = (OutputPath + "-Virtual").str();
      IndirectFileName = (OutputPath + "-Indirect").str();
    }

    std::string VirtualFileNameExtended = (Twine(VirtualFileName) + ".csv").str();
    std::string IndirectFileNameExtended = (Twine(IndirectFileName) + ".csv").str();
    if (sys::fs::exists(VirtualFileNameExtended) || sys::fs::exists(IndirectFileNameExtended)) {
      uint number = 1;
      while (sys::fs::exists(VirtualFileName + Twine(number) + ".csv")
             || sys::fs::exists(VirtualFileName + Twine(number) + ".csv")) {
        number++;
      }
      VirtualFileNameExtended = (VirtualFileName + Twine(number) + ".csv").str();
      IndirectFileNameExtended = (IndirectFileName + Twine(number) + ".csv").str();
    }

    return {VirtualFileNameExtended, IndirectFileNameExtended};
  };

  bool isVirtualFunction(const Function &F) {
    return AllVFunctions.find(F.getName()) != AllVFunctions.end() || F.getName().startswith("_ZTh");
  }

  bool isBlackListed(const Function &F) {
    return (F.getName().startswith("llvm.") || F.getName().startswith("__")  || F.getName() == "_Znwm");
  }

};

char SDAnalysis::ID = 0;

INITIALIZE_PASS(SDAnalysis, "sdAnalysis", "Build return ranges", false, false)

ModulePass *llvm::createSDAnalysisPass() {
  return new SDAnalysis();
}