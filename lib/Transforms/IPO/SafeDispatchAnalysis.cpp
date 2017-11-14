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

#include "llvm/Transforms/IPO/SafeDispatchLayoutBuilder.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"
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
    VirtualCallsiteInfo(std::string _FunctionName,
                        std::string _Dwarf,
                        std::string _ClassName,
                        std::string _PreciseName,
                        int _Params) :

            FunctionName(_FunctionName),
            Dwarf(_Dwarf),
            ClassName(_ClassName),
            PreciseName(_PreciseName),
            Params(Params)
    {}

  public:
    std::string FunctionName;
    std::string Dwarf;
    int Params;
    std::string ClassName;
    std::string PreciseName;
    int SubhierarchyCount;
    int PreciseSubhierarchyCount;
    int TargetSignatureMatches;
    int ShortTargetSignatureMatches;
    int PreciseTargetSignatureMatches;
    int NumberOfParamMatches;
    int SizeOfVTableIsland;
  };

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

  SDBuildCHA *CHA;
  std::vector<VirtualCallsiteInfo> Data;


  /** Start function type matching data */
  typedef std::pair<std::string, uint64_t> PreciseFunctionSignature;

  std::map<PreciseFunctionSignature, int> PreciseFunctionSignatureCount;
  std::map<uint64_t, int> FunctionWithEncodingCount;
  std::map<uint64_t, int> FunctionWithEncodingShortCount;

  std::map<std::string, Encodings> FunctionToEncoding;
  int NumberOfParameters[8];

  /** End function type matching data */

  /** Start hierarchy analysis data */

  typedef std::map<uint64_t, SDBuildCHA::func_name_t> offset_to_func_name;
  typedef std::map<uint64_t, std::set<SDBuildCHA::func_name_t>> offset_to_func_name_set;

  std::map<SDBuildCHA::vtbl_t, offset_to_func_name> FunctionNameInVTableAtOffset;
  std::map<SDBuildCHA::vtbl_name_t, offset_to_func_name_set> FunctionNamesInClassAtOffset;

  std::map<SDBuildCHA::vtbl_t, std::set<SDBuildCHA::vtbl_t>> VTableSubHierarchy;
  std::map<SDBuildCHA::vtbl_name_t, std::set<SDBuildCHA::vtbl_name_t>> ClassSubHierarchy;

  std::map<SDBuildCHA::func_and_class_t, std::set<SDBuildCHA::func_name_t>> VTableSubHierarchyPerFunction;
  std::map<SDBuildCHA::func_and_class_t, std::set<SDBuildCHA::func_name_t>> ClassSubHierarchyPerFunction;

  /** End hierarchy analysis data */

  /** Start binary vtable tool data */
  uint64_t TotalNumberOfDefinedVTables;

  std::map<SDBuildCHA::vtbl_t, std::set<SDBuildCHA::vtbl_t>*> VTableToIsland;

  /** End binary vtable tool data */


  bool runOnModule(Module &M) override {
    sdLog::blankLine();
    sdLog::stream() << "P7a. Started running the SDAnalysis pass ..." << sdLog::newLine << "\n";

    CHA = &getAnalysis<SDBuildCHA>();

    analyseCHA();

    computeVTableIslands();

    processVirtualFunctions(M);
    processVirtualCallSites(M);

    storeData(M);

    sdLog::stream() << sdLog::newLine << "P7a. Finished running the SDAnalysis pass ..." << "\n";
    sdLog::blankLine();
    return false;
  }

  void analyseCHA() {
    std::cout << "Building hierarchies..." << std::endl;
    buildSubHierarchies();
    std::cout << "Computing targets in VTable hierarchy..." << std::endl;
    matchTargetsInVTableHierarchy();
    std::cout << "Computing targets in Class hierarchy..." << std::endl;
    matchTargetsInClassHierarchy();
    std::cout << "Finished hierarchy analysis!" << std::endl;

    for (auto &entry : VTableSubHierarchyPerFunction) {
      std::cout << entry.first.second << ", " << entry.first.first << ":";
      for (auto name : entry.second) {
        std::cout << " " << name;
      }
      std::cout << std::endl;
    }
  }

  void buildSubHierarchies() {
    auto topologicalOrder = CHA->topoSort();
    for (auto className = topologicalOrder.rbegin(); className != topologicalOrder.rend(); ++className) {
      auto vTableList = CHA->getSubVTables(*className);
      std::cout << "\t" << *className << " with " << vTableList.size() << " vTables:" << std::endl;

      std::set<SDBuildCHA::vtbl_name_t> classChildren;
      offset_to_func_name_set functionNamesAtOffset;
      int vTableIndex = 0;
      for (auto &vTableType : vTableList) {
        auto vTable = SDBuildCHA::vtbl_t(*className, vTableIndex);
        std::cout << "\t(" << vTable.first << ", " << vTable.second << ") of type " << vTableType << std::endl;

        for (auto functionEntry : CHA->getFunctionEntries(vTable)) {
          std::cout << "\t\t" << functionEntry.functionName << "@" << functionEntry.offsetInVTable << std::endl;
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
        functionNames.insert(functionNameEntry.second);

        for (auto &vTable : subHierarchy.second) {
          functionNames.insert(FunctionNameInVTableAtOffset[vTable][offsetInVTable]);
        }

        VTableSubHierarchyPerFunction[SDBuildCHA::func_and_class_t(functionNameEntry.second, rootVTable.first)]
                = functionNames;
      }
    }
  }

  void matchTargetsInClassHierarchy() {
    for (auto &subHierarchy : ClassSubHierarchy) {
      auto rootClassName = subHierarchy.first;
      for (auto &functionNameEntry : FunctionNamesInClassAtOffset.find(rootClassName)->second) {
        auto offsetInVTable = functionNameEntry.first;

        std::set<SDBuildCHA::func_name_t> functionNames;
        functionNames.insert(functionNameEntry.second.begin(), functionNameEntry.second.end());

        for (auto &className : subHierarchy.second) {
          auto &functionNamesInChild  = FunctionNamesInClassAtOffset[className][offsetInVTable];
          functionNames.insert(functionNamesInChild.begin(), functionNamesInChild.end());
        }

        for (auto &functionName : functionNameEntry.second) {
          ClassSubHierarchyPerFunction[SDBuildCHA::func_and_class_t(functionName, rootClassName)]
                  = functionNames;
        }
      }
    }
  }

  void countAllVTables() {
    std::set<SDBuildCHA::vtbl_t> vtables;
    int sum = 0;
    for (auto itr = CHA->roots_begin(); itr != CHA->roots_end(); ++itr) {
      auto root = SDBuildCHA::vtbl_t(*itr, 0);
      for(auto &vTable : CHA->preorder(root)) {
        if (CHA->isDefined(vTable))
          vtables.insert(vTable);
      }
      sum += CHA->getCloudSize(root.first);
    }
    std::cout << "Number of VTables: " << vtables.size() << " (sum of clouds:" << sum << ")" << std::endl;
  }

  void computeVTableIslands() {
    std::vector<std::set<SDBuildCHA::vtbl_t>*> islands;

    for (auto itr = CHA->roots_begin(); itr != CHA->roots_end(); ++itr) {
      auto root = SDBuildCHA::vtbl_t(*itr, 0);
      std::set<SDBuildCHA::vtbl_t> *island = new std::set<SDBuildCHA::vtbl_t>();
      bool isNewIsland = true;

      for (auto &vTable : CHA->preorder(root)) {
        if (CHA->isDefined(vTable)) {
          if (VTableToIsland.find(vTable) != VTableToIsland.end()) {
            VTableToIsland[vTable]->insert(island->begin(), island->end());
            island = VTableToIsland[vTable];
            isNewIsland = false;
          }
          VTableToIsland[vTable] = island;
          island->insert(vTable);
        }
      }
      if (isNewIsland) {
        islands.push_back(island);
      }
    }

    TotalNumberOfDefinedVTables = VTableToIsland.size();
    std::cout << "Total Number of VTables: " << VTableToIsland.size() << std::endl;
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

      for(int i = 0; i <= F.getFunctionType()->getNumParams() && i <= 7; ++i) {
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

    VirtualCallsiteInfo CallSiteInfo(FunctionName, Dwarf, ClassName, PreciseName, CallSite.getFunctionType()->getNumParams());

    //sub-hierarchy
    CallSiteInfo.SubhierarchyCount =
            ClassSubHierarchyPerFunction[SDBuildCHA::func_and_class_t(FunctionName, PreciseName)].size();

    CallSiteInfo.PreciseSubhierarchyCount =
            VTableSubHierarchyPerFunction[SDBuildCHA::func_and_class_t(FunctionName, PreciseName)].size();

    //src types, safe src types
    auto EncodingsPtr = FunctionToEncoding.find(FunctionName);
    if (EncodingsPtr == FunctionToEncoding.end())
      return;

    Encodings Encodings = EncodingsPtr->second;
    CallSiteInfo.TargetSignatureMatches = FunctionWithEncodingCount[Encodings.Normal];
    CallSiteInfo.ShortTargetSignatureMatches = FunctionWithEncodingShortCount[Encodings.Short];
    auto ParentFunctionName = CHA->getFunctionRootParent(FunctionName);
    CallSiteInfo.PreciseTargetSignatureMatches = PreciseFunctionSignatureCount[PreciseFunctionSignature(ParentFunctionName,Encodings.Precise)];

    auto NumberOfParam = CallSite.getFunctionType()->getNumParams();
    if (NumberOfParam >= 7)
      NumberOfParam = 7;
    CallSiteInfo.NumberOfParamMatches = NumberOfParameters[NumberOfParam];

    //TODO MATT: subvtable 0 is wrong! Figure out which vtable is being used, by transfering the data from the frontend.
    CallSiteInfo.NumberOfParamMatches = VTableToIsland[SDBuildCHA::vtbl_t(PreciseName, 0)]->size();

    sdLog::log() << "Finished callsite: " << Dwarf << "\n";
    Data.push_back(CallSiteInfo);
  }

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
      Outfile << "Dwarf"
              << ",FunctionName"
              << ",ClassName"
              << ",PreciseName"
              << ",Params"
              << ",ClassSubHierarchy"
              << ",VTableSubHierarchy"
              << ",SafeSrcType"
              << ",SrcType"
              << ",PreciseSrcType"
              << ",BinType"
              << "VTableIsland"
              << "\n";

      for (auto &Callsite : Data) {
        Outfile << Callsite.Dwarf
                << "," << Callsite.FunctionName
                << "," << Callsite.ClassName
                << "," << Callsite.PreciseName
                << "," << Callsite.Params
                << "," << Callsite.SubhierarchyCount
                << "," << Callsite.PreciseSubhierarchyCount
                << "," << Callsite.ShortTargetSignatureMatches
                << "," << Callsite.TargetSignatureMatches
                << "," << Callsite.PreciseTargetSignatureMatches
                << "," << Callsite.NumberOfParamMatches
                << "," << Callsite.SizeOfVTableIsland
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