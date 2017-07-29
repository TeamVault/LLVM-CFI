#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

#include "llvm/Transforms/IPO/SafeDispatchReturnRange.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"

#include <string>
#include <fstream>
#include <sstream>


// you have to modify the following 4 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManasdgerBuilder.cpp

using namespace llvm;

char SDReturnRange::ID = 0;
INITIALIZE_PASS(SDReturnRange, "sdRetRange", "Build return ranges", false, false)

ModulePass* llvm::createSDReturnRangePass() {
  return new SDReturnRange();
}

//TODO MATT: format properly / code duplication
static string sd_getClassNameFromMD(llvm::MDNode* mdNode, unsigned operandNo = 0) {
//  llvm::MDTuple* mdTuple = dyn_cast<llvm::MDTuple>(mdNode);
//  assert(mdTuple);

  llvm::MDTuple* mdTuple = cast<llvm::MDTuple>(mdNode);
  assert(mdTuple->getNumOperands() > operandNo + 1);

//  llvm::MDNode* nameMdNode = dyn_cast<llvm::MDNode>(mdTuple->getOperand(operandNo).get());
//  assert(nameMdNode);
  llvm::MDNode* nameMdNode = cast<llvm::MDNode>(mdTuple->getOperand(operandNo).get());

//  llvm::MDString* mdStr = dyn_cast<llvm::MDString>(nameMdNode->getOperand(0));
//  assert(mdStr);
  llvm::MDString* mdStr = cast<llvm::MDString>(nameMdNode->getOperand(0));

  StringRef strRef = mdStr->getString();
  assert(sd_isVtableName_ref(strRef));

//  llvm::MDNode* gvMd = dyn_cast<llvm::MDNode>(mdTuple->getOperand(operandNo+1).get());
  llvm::MDNode* gvMd = cast<llvm::MDNode>(mdTuple->getOperand(operandNo+1).get());

//  SmallString<256> OutName;
//  llvm::raw_svector_ostream Out(OutName);
//  gvMd->print(Out, CURR_MODULE);
//  Out.flush();

  llvm::ConstantAsMetadata* vtblConsMd = dyn_cast_or_null<ConstantAsMetadata>(gvMd->getOperand(0).get());
  if (vtblConsMd == NULL) {
//    llvm::MDNode* tmpnode = dyn_cast<llvm::MDNode>(gvMd);
//    llvm::MDString* tmpstr = dyn_cast<llvm::MDString>(tmpnode->getOperand(0));
//    assert(tmpstr->getString() == "NO_VTABLE");

    return strRef.str();
  }

//  llvm::GlobalVariable* vtbl = dyn_cast<llvm::GlobalVariable>(vtblConsMd->getValue());
//  assert(vtbl);
  llvm::GlobalVariable* vtbl = cast<llvm::GlobalVariable>(vtblConsMd->getValue());

  StringRef vtblNameRef = vtbl->getName();
  assert(vtblNameRef.startswith(strRef));

  return vtblNameRef.str();
}

void SDReturnRange::locateCallSites(Module &M) {
  Function *sd_vtbl_indexF = M.getFunction(Intrinsic::getName(Intrinsic::sd_get_checked_vptr));

  if (!sd_vtbl_indexF){
    sd_print("ERROR");
    return;
  }

  // for each use of the function
  for (const Use &U : sd_vtbl_indexF->uses()) {

    // get intrinsic call instruction
    llvm::CallInst *CI = dyn_cast<CallInst>(U.getUser());
    assert(CI);

    // find actual call
    //TODO MATT: unsafe use-def chain (FIX: traverse the chain in reverse (from callSite to intrinsic))
    llvm::User *user = *(CI->users().begin());
    for (int i = 0; i < 3; ++i) {
      //user->dump();
      for(User *next : user->users()) {
        user = next;
        break;
      }
    }

    if (CallInst *CallSite = dyn_cast<CallInst>(user)) {
      // valid virtual CallSite
      addCallSite(CI, *CallSite);
    }
  }
}

void SDReturnRange::addCallSite(const CallInst* checked_vptr_call, CallInst &callSite) {
  // get the v ptr
  llvm::Value *vptr = checked_vptr_call->getArgOperand(0);
  assert(vptr);//assert not null

  //Paul: get second operand
  llvm::MetadataAsValue *arg2 = dyn_cast<MetadataAsValue>(checked_vptr_call->getArgOperand(1));
  assert(arg2);//assert not null

  //Paul: get the metadata of the second param
  MDNode *mdNode = dyn_cast<MDNode>(arg2->getMetadata());
  assert(mdNode);//assert not null

  //Paul: get the third parameter
  llvm::MetadataAsValue *arg3 = dyn_cast<MetadataAsValue>(checked_vptr_call->getArgOperand(2));
  assert(arg3);//assert not null

  //Paul: get the metadata of the third param
  MDNode *mdNode1 = dyn_cast<MDNode>(arg3->getMetadata());
  assert(mdNode1);//assert not null

  // second one is the tuple that contains the class name and the corresponding global var.
  // note that the global variable isn't always emitted
  //get the class name class name from argument 1
  string className = sd_getClassNameFromMD(mdNode, 0);

  //get a more precise class name from argument 2
  string preciseName = sd_getClassNameFromMD(mdNode1, 0);

  // write DebugLoc to map (is written to file in storeCallSites)
  std::stringstream Stream;
  const DebugLoc &Log = callSite.getDebugLoc();
  auto *Scope = cast<MDScope>(Log.getScope());
  Stream << Scope->getFilename().str() << ":" << Log.getLine() << ":" << Log.getCol()
         << "," << className << "," << preciseName;
  callSiteDebugLocs.push_back(Stream.str());

  sdLog::stream() << "Callsite @" <<Scope->getFilename().str() << ":" << Log.getLine() << ":" << Log.getCol()
                  << " for class " << className << " (" << preciseName << ")\n";

  // emit a SubclassHierarchy for preciseName if its not already emitted
  emitSubclassHierarchyIfNeeded(className);
}

// helper for emitSubclassHierarchyIfNeeded
void SDReturnRange::createSubclassHierarchy(const SDBuildCHA::vtbl_t &root, std::set<string> &output) {
  for (auto it = cha->children_begin(root); it != cha->children_end(root); it++) {
    const SDBuildCHA::vtbl_t &child = *it;
    output.insert(child.first);
    errs() << "inserting: " << child.first << "\n";
    createSubclassHierarchy(child, output);
  }
}

void SDReturnRange::emitSubclassHierarchyIfNeeded(std::string rootClassName) {
  if (emittedClassHierarchies.find(rootClassName) != emittedClassHierarchies.end())
    return;

  sdLog::stream() << "Emitting hierarchy for " << rootClassName << ":\n";
  std::set<string> SubclassSet;
  SubclassSet.insert(rootClassName);
  createSubclassHierarchy(SDBuildCHA::vtbl_t(rootClassName, 0), SubclassSet);
  emittedClassHierarchies[rootClassName] = SubclassSet;
  for (auto &element: SubclassSet) {
    errs() << element << " , ";
  }
  errs() << "\n";
}

void SDReturnRange::storeCallSites(Module &M) {
  //TODO MATT: Handle multiple LTO-Modules (write to different files or filesections?)
  sdLog::stream()  << "storeCallSites: " << M.getName() << "\n";
  std::ofstream output_file("./_SD_CallSites.txt");
  std::ostream_iterator<std::string> output_iterator(output_file, "\n");
  std::copy(callSiteDebugLocs.begin(), callSiteDebugLocs.end(), output_iterator);
}

void SDReturnRange::storeClassHierarchy(Module &M) {
  //TODO MATT: We could store to NamedMDNode instead of external files!
  sdLog::stream()  << "storeClassHierarchy: " << M.getName() << "\n";
  std::ofstream output_file("./_SD_ClassHierarchy.txt");
  for (auto& mapEntry : emittedClassHierarchies) {
    output_file << mapEntry.first;
    for (auto& element : mapEntry.second) {
      output_file << "," << element;
    }
    output_file << "\n";
  }
}