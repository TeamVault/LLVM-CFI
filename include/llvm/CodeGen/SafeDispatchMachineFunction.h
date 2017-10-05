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

  SDMachineFunction() : MachineFunctionPass(ID),
                        CallSiteDebugLoc() {
    sdLog::stream() << "initializing SDMachineFunction pass\n";
    initializeSDMachineFunctionPass(*PassRegistry::getPassRegistry());

    //TODO MATT: STOPPER FOR DEBUG
    //std::string stopper;
    //std::cin >> stopper;
    //errs() << stopper;

    unknownID = 0xFFFFF;
    indirectID = 0xFEFEF;
    tailID = 0xFEFEF;

    loadCallSiteData();
    loadStaticCallSiteData();
    loadStaticFunctionIDData();
  }

  virtual ~SDMachineFunction() {
    uint64_t sum;
    if (!rangeWidths.empty()) {
      for (int i : rangeWidths) {
        sum += i;
      }
      double avg = double(sum) / rangeWidths.size();
      sdLog::stream() << "AVG RANGE: " << avg << "\n";
      sdLog::stream() << "TOTAL RANGES: " << rangeWidths.size() << "\n";
    }
    sdLog::stream() << "deleting SDMachineFunction pass\n";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.setPreservesAll();
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (MF.getMMI().getModule()->getNamedMetadata("SD_emit_return_labels") == nullptr)
      return false;

    sdLog::log() << "Running SDMachineFunction pass: "<< MF.getName() << "\n";
    auto TII = MF.getSubtarget().getInstrInfo();
    //We would get NamedMetadata like this:
    //const auto &M = MF.getMMI().getModule();
    //const auto &MD = M->getNamedMetadata("sd.class_info._ZTV1A");
    //MD->dump();
    for (auto &MBB: MF) {
      for (auto &MI : MBB) {
        if (MI.isCall()) {

          auto debugLocString = debugLocToString(MI.getDebugLoc());
          auto classNameIt = CallSiteDebugLoc.find(debugLocString);
          if (classNameIt != CallSiteDebugLoc.end()) {

            auto className = classNameIt->second;
            sdLog::log() << "Machine CallInst: @" << debugLocString
                         << ": " << className  << "\n";

            if (CallSiteRange.find(debugLocString) == CallSiteRange.end()) {
              sdLog::errs() << debugLocString << " has not Range!";
              continue;
            }

            auto range = CallSiteRange[debugLocString];
            rangeWidths.push_back(range.second - range.first);
            TII->insertNoop(MBB, MI.getNextNode());
            MI.getNextNode()->operands_begin()[3].setImm((range.second - range.first) | 0x80000);
            TII->insertNoop(MBB, MI.getNextNode());
            MI.getNextNode()->operands_begin()[3].setImm(range.first | 0x80000);
            continue;
          }

          auto FunctionNameIt = CallSiteDebugLocStatic.find(debugLocString);
          if (FunctionNameIt != CallSiteDebugLocStatic.end()) {

            auto FunctionName = FunctionNameIt->second;
            sdLog::log() << "Machine CallInst in " << MF.getName() << "@" << debugLocString
                            << " is static caller for: " << FunctionName << "\n";

            if (FunctionName == "__UNDEFINED__") {
              TII->insertNoop(MBB, MI.getNextNode());
              MI.getNextNode()->operands_begin()[3].setImm(indirectID);
              continue;
            }

            if (FunctionName == "__TAIL__") {
              TII->insertNoop(MBB, MI.getNextNode());
              MI.getNextNode()->operands_begin()[3].setImm(tailID);
              continue;
            }

            if (FunctionIDMap.find(FunctionName) == FunctionIDMap.end()) {
              sdLog::errs() << FunctionName << " has not ID!";
              continue;
            }

            auto ID = FunctionIDMap[FunctionName];

            TII->insertNoop(MBB, MI.getNextNode());
            MI.getNextNode()->operands_begin()[3].setImm(ID | 0x80000);
            continue;
          }

          if (MI.getNumOperands() > 0 && !MI.getOperand(0).isGlobal()) {
            TII->insertNoop(MBB, MI.getNextNode());
            MI.getNextNode()->operands_begin()[3].setImm(unknownID);

            sdLog::warn() << "Unknown call in " << MF.getName() << " (" << debugLocString << "," << MI << ")" << " gets ID "
                          << unknownID << "!\n";
          }
        }
      }
    }

    sdLog::log() << "Finished SDMachineFunction pass: "<< MF.getName() << "\n";

    return true;
  }

  void loadCallSiteData() {
    //TODO MATT: delete file
    std::ifstream InputFile("./_SD_CallSites.txt");
    std::string InputLine;
    std::string DebugLoc, ClassName, PreciseName, FunctionName;
    std::string MinStr, MaxStr;


    while (std::getline(InputFile, InputLine)) {
      std::stringstream LineStream(InputLine);

      std::getline(LineStream, DebugLoc, ',');
      std::getline(LineStream, ClassName, ',');
      std::getline(LineStream, PreciseName, ',');
      std::getline(LineStream, FunctionName, ',');
      std::getline(LineStream, MinStr, ',');
      LineStream >> MaxStr;
      int min = std::stoi(MinStr);
      int max = std::stoi(MaxStr);
      CallSiteDebugLoc[DebugLoc] = ClassName;
      CallSiteRange[DebugLoc] = {min, max};
    }
  }

  void loadStaticCallSiteData() {
    //TODO MATT: delete file
    std::ifstream InputFile("./_SD_CallSitesStatic.txt");
    std::string InputLine;
    std::string DebugLoc, FunctionName;

    while (std::getline(InputFile, InputLine)) {
      std::stringstream LineStream(InputLine);

      std::getline(LineStream, DebugLoc, ',');
      LineStream >> FunctionName;
      CallSiteDebugLocStatic[DebugLoc] = FunctionName;
    }
  }

  void loadStaticFunctionIDData() {
    //TODO MATT: delete file
    std::ifstream InputFile("./_SD_FunctionIDMap.txt");
    std::string InputLine;
    std::string FunctionName, IDString;

    while (std::getline(InputFile, InputLine)) {
      std::stringstream LineStream(InputLine);

      std::getline(LineStream, FunctionName, ',');
      LineStream >> IDString;
      int ID = std::stoi(IDString);
      FunctionIDMap[FunctionName] = ID;
    }
  }

private:
  std::map <std::string, std::string> CallSiteDebugLoc;
  std::map <std::string, std::pair<int, int>> CallSiteRange;
  std::map <std::string, std::string> CallSiteDebugLocStatic;
  std::map <std::string, int> FunctionIDMap;

  static std::string debugLocToString(const DebugLoc &Log);
  std::vector<int> rangeWidths;
  static int count;
  uint64_t unknownID;
  uint64_t indirectID;
  uint64_t tailID;
};
}

#endif //LLVM_SAFEDISPATCHMACHINEFUNCION_H