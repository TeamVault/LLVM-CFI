#ifndef LLVM_SAFEDISPATCHMACHINEFUNCION_H
#define LLVM_SAFEDISPATCHMACHINEFUNCION_H

#include "llvm/Transforms/IPO/SafeDispatchReturnRange.h"
#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

#include "llvm/MC/MCContext.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/MC/MCSymbol.h"


#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

namespace llvm {
/**
 * This pass receives information generated in the SafeDispatch LTO passes
 * (SafeDispatchReturnRange) for use in the X86 backend.
 * */

struct SDMachineFunction : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  SDMachineFunction() : MachineFunctionPass(ID) {
    sdLog::stream() << "initializing SDMachineFunction pass\n";
    initializeSDMachineFunctionPass(*PassRegistry::getPassRegistry());

    //TODO MATT: STOPPER FOR DEBUG
    //std::string stopper;
    //std::cin >> stopper;
    //errs() << stopper;

    loadVirtualCallSiteData();
    loadStaticCallSiteData();
    loadStaticFunctionIDData();
  }

  virtual ~SDMachineFunction() {
    analyse();
    sdLog::stream() << "deleting SDMachineFunction pass\n";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  // Constants
  const int64_t unknownID = 0xFFFFF;
  const int64_t indirectID = 0xFFFFF;
  const int64_t tailID = 0xFEFEF;

  // Data
  std::map <std::string, std::string> CallSiteDebugLocVirtual;
  std::map <std::string, std::string> CallSiteDebugLocStatic;

  std::map <std::string, std::pair<int64_t , int64_t>> CallSiteRange;
  std::map <std::string, int64_t> FunctionIDMap;

  // Functions
  void loadVirtualCallSiteData();
  void loadStaticCallSiteData();
  void loadStaticFunctionIDData();

  bool processVirtualCallSite(std::string &DebugLocString,
                              MachineInstr &MI,
                              MachineBasicBlock &MBB,
                              const TargetInstrInfo *TII);

  bool processStaticCallSite(std::string &DebugLocString,
                             MachineInstr &MI,
                             MachineBasicBlock &MBB,
                             const TargetInstrInfo *TII);

  bool processUnknownCallSite(std::string &DebugLocString,
                              MachineInstr &MI,
                              MachineBasicBlock &MBB,
                              const TargetInstrInfo *TII);

  std::string debugLocToString(const DebugLoc &Log);

  // Analysis
  std::vector<int64_t> RangeWidths;
  std::map <int64_t, int> IDCount;

  int NumberOfVirtual;
  int NumberOfStaticDirect;
  int NumberOfIndirect;
  int NumberOfTail;
  int NumberOfUnknown;

  void analyse();
};
}

#endif //LLVM_SAFEDISPATCHMACHINEFUNCION_H