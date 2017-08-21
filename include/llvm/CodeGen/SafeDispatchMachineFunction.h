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
                        CallSiteDebugLoc(),
                        CallSiteMap() {
    sdLog::stream() << "initializing SDMachineFunction pass\n";
    initializeSDMachineFunctionPass(*PassRegistry::getPassRegistry());

    //TODO MATT: STOPPER FOR DEBUG
    //std::string stopper;
    //std::cin >> stopper;

    loadCallSiteData();
    loadStaticCallSiteData();
    loadCallHierarchyData();
  }

  virtual ~SDMachineFunction() {
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

            //create label
            std::stringstream ss;
            ss << "SD_LABEL_" << count++;
            auto name = ss.str();
            MCSymbol *symbol = MF.getContext().GetOrCreateSymbol(name);

            auto range = CallSiteRange[debugLocString];
            errs() << "range: " << range.first << "-" << range.second << "\n";

            TII->insertNoop(MBB, MI.getNextNode());
            MI.getNextNode()->operands_begin()[3].setImm((range.second - range.first) | 0x80000);
            MI.getNextNode()->dump();

            TII->insertNoop(MBB, MI.getNextNode());
            MI.getNextNode()->operands_begin()[3].setImm(range.first | 0x80000);
            MI.getNextNode()->dump();

            BuildMI(MBB, MI.getNextNode(), MI.getDebugLoc(), TII->get(TargetOpcode::EH_LABEL))
                    .addSym(symbol);
            //BuildMI(MBB, MI.getNextNode(), MI.getDebugLoc(), TII->get(X86::NOOPL)).addImm(range.first);

            // insert MI into the vectors for the base class and all of its subclasses!
            for (auto &SubClass : ClassHierarchies[className]) {
              insert(SubClass, MI, MF, symbol);
            }
            sdLog::log() << "\n";
            continue;
          }

          auto FunctionNameIt = CallSiteDebugLocStatic.find(debugLocString);
          if (FunctionNameIt != CallSiteDebugLocStatic.end()) {

            auto FunctionName = FunctionNameIt->second;
            sdLog::log() << "Machine CallInst in " << MF.getName() << "@" << debugLocString
                            << " is static caller for: " << FunctionName << "\n";

            auto globalMin = MF.getMMI().getModule()->getGlobalVariable("_SD_RANGE_" + FunctionName + "_min");
            if (globalMin == nullptr) {
              sdLog::log() << "No global found...\n";
              continue;
            }

            //create label
            std::stringstream ss;
            ss << "SD_LABEL_" << count++;
            auto name = ss.str();
            MCSymbol *Label = MF.getContext().GetOrCreateSymbol(name);
            BuildMI(MBB, MI.getNextNode(), MI.getDebugLoc(), TII->get(TargetOpcode::EH_LABEL))
                    .addSym(Label);


            if (RangeBounds.find(FunctionName) == RangeBounds.end()) {
              globalMin->setConstant(true);
              RangeBounds[FunctionName].first = debugLocToString(MI.getDebugLoc());
              Labels["_SD_RANGE_" + FunctionName + "_min"] = Label;
              sdLog::log() << "min: " << "_SD_RANGE_" << FunctionName << "_min" << "\n";
            }

            auto globalMax = MF.getMMI().getModule()->getGlobalVariable("_SD_RANGE_" + FunctionName + "_max");
            if (globalMax == nullptr) {
              sdLog::warn() << "No global: "<< "_SD_RANGE_" << FunctionName << "_max" << "\n";
              continue;
            }
            globalMax->setConstant(true);
            RangeBounds[FunctionName].second = debugLocToString(MI.getDebugLoc());
            Labels["_SD_RANGE_" + FunctionName + "_max"] = Label;
            sdLog::log() << "max: " << "_SD_RANGE_" << FunctionName << "_max" << "\n";
            sdLog::log() << "\n";
            continue;
          }
          sdLog::log() << "Unknown call (" << debugLocString << ") for " << MF.getName() << "!\n";
        }
      }
    }

    sdLog::log() << "Finished SDMachineFunction pass: "<< MF.getName() << "\n";

    return true;
  }

  void insert(std::string className, MachineInstr &MI, MachineFunction &MF, MCSymbol *Label) {
    sdLog::log() << "Call is valid for class: " << className << "\n";
    CallSiteMap[className].push_back(&MI);

    if (RangeBounds.find(className) == RangeBounds.end()) {

      auto global = MF.getMMI().getModule()->getGlobalVariable("_SD_RANGE_" + className + "_min");
      if (global == nullptr) {
        sdLog::warn() << "No global: "<< "_SD_RANGE_" << className << "_min" << "\n";
        return;
      }
      global->setConstant(true);

      RangeBounds[className].first = debugLocToString(MI.getDebugLoc());
      Labels["_SD_RANGE_" + className + "_min"] = Label;
      sdLog::log() << "min: " << "_SD_RANGE_" << className << "_min" << "\n";

    }

    auto global = MF.getMMI().getModule()->getGlobalVariable("_SD_RANGE_" + className + "_max");
    if (global == nullptr) {
      sdLog::warn() << "No global: "<< "_SD_RANGE_" << className << "_max" << "\n";
      return;
    }
    global->setConstant(true);

    RangeBounds[className].second = debugLocToString(MI.getDebugLoc());
    Labels["_SD_RANGE_" + className + "_max"] = Label;
    sdLog::log() << "max: " << "_SD_RANGE_" << className << "_max" << "\n";
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

  void loadCallHierarchyData() {
    //TODO MATT: delete file
    std::ifstream InputFile("./_SD_ClassHierarchy.txt");
    std::string Input;
    std::string BaseClass;

    while (std::getline(InputFile, Input)) {
      std::stringstream LineStream(Input);

      std::getline(LineStream, BaseClass, ',');
      std::vector <std::string> SubClasses;
      while (std::getline(LineStream, Input, ',')) {
        SubClasses.push_back(Input);
      }

      ClassHierarchies[BaseClass] = SubClasses;
    }
  }

  static MCSymbol *getLabelForGlobal(Twine globalName) {
    return Labels[globalName.str()];
  }

private:
  std::map <std::string, std::string> CallSiteDebugLoc;
  std::map <std::string, std::pair<int, int>> CallSiteRange;
  std::map <std::string, std::string> CallSiteDebugLocStatic;
  std::map <std::string, std::vector<std::string>> ClassHierarchies;

  std::map <std::string, std::vector<MachineInstr *>> CallSiteMap;
  static std::map <std::string, std::pair<std::string, std::string>> RangeBounds;
  static std::map<std::string, MCSymbol *> Labels;

  static std::string debugLocToString(const DebugLoc &Log);

  static int count;
};
}

#endif //LLVM_SAFEDISPATCHMACHINEFUNCION_H