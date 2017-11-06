//===-- llvm/Transforms/IPO/SafeDispatchReturnRange.h --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a ModulePass for the SafeDispatch backward edge protection.
// It is used to analyse all call sites and generate all the information
// about them that is needed by the backend to insert the return range checks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SAFEDISPATCHRETURNRANGE_H
#define LLVM_SAFEDISPATCHRETURNRANGE_H

#include "llvm/ADT/StringSet.h"
#include "llvm/Transforms/IPO/SafeDispatchCHA.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

namespace llvm {

struct SDReturnRange : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  static const std::string CONSTRUCTION_VTABLE_DEMANGLE_PREFIX;
  static const std::string VTABLE_DEMANGLE_PREFIX;

  SDReturnRange() : ModulePass(ID),
                    CallSiteDebugLocsVirtual(),
                    CallSiteDebugLocsStatic(),
                    CalledFunctions() {
    sdLog::stream() << "initializing SDReturnRange pass\n";
    initializeSDReturnRangePass(*PassRegistry::getPassRegistry());

    CurrentFunctionTypeID = 0x7FFFE;
    pseudoDebugLoc = 1;
  }

  virtual ~SDReturnRange() {
    sdLog::stream() << "deleting SDReturnRange pass\n";
  }

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SDBuildCHA>();
    AU.addPreserved<SDBuildCHA>();
  }

  const StringSet<> *getStaticCallees() {
    return &CalledFunctions;
  }

  const std::map<uint64_t, uint32_t> *getFunctionTypeIDMap() {
    return &FunctionTypeIDMap;
  };

private:
  SDBuildCHA *CHA;

  /// Information about the virtual CallSites that are being found by this pass.
  std::vector<std::string> CallSiteDebugLocsVirtual;

  /// Information about the static CallSites that are being found by this pass.
  std::vector<std::string> CallSiteDebugLocsStatic;

  /// Set of static Callees.
  StringSet<> CalledFunctions;

  /// Set of all virtual CallSites.
  std::set<CallSite> VirtualCallSites;

  std::map<uint64_t, uint32_t> FunctionTypeIDMap;

  uint32_t CurrentFunctionTypeID;

  /// Current ID for the DebugLoc hack.
  uint64_t pseudoDebugLoc;

  /// Find and process all virtual CallSites.
  void processVirtualCallSites(Module &M);

  /// Find and process all static callSites.
  void processStaticCallSites(Module &M);

  /// Extract the CallSite information from @param CheckedVptrCall and add the CallSite to CallSiteDebugLocsVirtual.
  void addVirtualCallSite(const CallInst *CheckedVptrCall, CallSite CallSite, Module &M);

  /// Add the CallSite to CallSiteDebugLocsStatic.
  void addStaticCallSite(CallSite CallSite, Module &M);

  /// Store all callSite information (later retrieved by the backend).
  void storeCallSites(Module &M);

  /// Get or create a DebugLoc for CallSite (created by using pseudoDebugLoc).
  const DebugLoc* getOrCreateDebugLoc(CallSite CallSite, Module &M);

  uint32_t encodeFunction(FunctionType* FuncTy);
};

}

#endif //LLVM_SAFEDISPATCHRETURNRANGE_H
