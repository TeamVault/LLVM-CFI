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

    static std::string demangleClassname(std::string mangledClassname) {
      std::string className = "";
      bool foundDigit = false;
      for (auto c : mangledClassname){
        if (isdigit(c)) {
          foundDigit = true;
        } else if (foundDigit) {
          className.push_back(c);
        }
      }
      return className;
    }

    struct SDMachineFunction : public MachineFunctionPass {
    public:
        static char ID; // Pass identification, replacement for typeid

        SDMachineFunction() : MachineFunctionPass(ID),
                              CallSiteDebugLoc(),
                              CallSiteMap() {
          sd_print("initializing SDMachineFunction pass\n");
          initializeSDMachineFunctionPass(*PassRegistry::getPassRegistry());

          //TODO MATT: STOPPER FOR DEBUG
          //std::string stopper;
          //std::cin >> stopper;

          loadCallSiteData();
          loadCallHierarchyData();
        }

        virtual ~SDMachineFunction() {
          sd_print("deleting SDMachineFunction pass\n");
        }

        void getAnalysisUsage(AnalysisUsage &AU) const {
          MachineFunctionPass::getAnalysisUsage(AU);
          AU.setPreservesAll();
        }

        bool runOnMachineFunction(MachineFunction &MF) override {
          sd_print("P??. Started running SDMachineFunction pass (%s) ...\n", MF.getName());
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
                if (classNameIt == CallSiteDebugLoc.end())
                  continue;

                auto className = classNameIt->second;
                sdLog::stream() << "Machine CallInst:\n" << MI
                                << "has callee base class: " << className << "\n";
                //create label
                sdLog::stream() << "\n";
                std::stringstream ss;
                ss << "SD_LABEL_" << count++;
                auto name = ss.str();
                MCSymbol *symbol = MF.getContext().GetOrCreateSymbol(name);
                //TODO MATT: getNextNode() potentially unsafe (?)
                BuildMI(MBB, MI.getNextNode(), MI.getDebugLoc(), TII->get(TargetOpcode::EH_LABEL))
                        .addSym(symbol);

                // insert MI into the vectors for the base class and all of its subclasses!
                for (auto &SubClass : ClassHierarchies[className]) {
                  insert(SubClass, MI, MF, symbol);
                }
              }
            }
          }

          //TODO MATT: fix pass number
          sd_print("P??. Finished running SDMachineFunction pass (%s) ...\n", MF.getName());
          //sdLog::stream() << *MF.getMMI().getModule()->getGlobalVariable("_SD_RANGESTUB_ZTV1E_min") << "\n";
          //sdLog::stream() << *MF.getMMI().getModule()->getGlobalVariable("_SD_RANGESTUB_ZTV1E_max") << "\n";
          for (auto &entry : RangeBounds) {
            auto bounds = entry.second;
            sdLog::stream() << entry.first
                            << ": (min: " << bounds.first
                            << " - max: " << bounds.second << ")\n";
          }

          return true;
        }

        void insert(std::string mangledClassname, MachineInstr &MI, MachineFunction &MF, MCSymbol *Label) {
          auto className = demangleClassname(mangledClassname);
          sdLog::stream() << "Call is valid for class: " << className << "\n";
          CallSiteMap[className].push_back(&MI);

          if (RangeBounds.find(className) == RangeBounds.end()) {

            auto global = MF.getMMI().getModule()->getGlobalVariable("_SD_RANGESTUB_" + className + "_min");
            if (global == nullptr) {
              sdLog::stream() << "No min global found...\n";
              return;
            }
            global->setConstant(true);

            RangeBounds[className].first = debugLocToString(MI.getDebugLoc());
            Labels["_SD_RANGESTUB_" + className + "_min"] = Label;
            sdLog::stream() << "min: " << "_SD_RANGESTUB_" << className << "_min" << "\n";

          }

          auto global = MF.getMMI().getModule()->getGlobalVariable("_SD_RANGESTUB_" + className + "_max");
          if (global == nullptr) {
            sdLog::stream() << "No max global found...\n";
            return;
          }
          global->setConstant(true);

          RangeBounds[className].second = debugLocToString(MI.getDebugLoc());
          Labels["_SD_RANGESTUB_" + className + "_max"] = Label;
          sdLog::stream() << "max: " << "_SD_RANGESTUB_" << className << "_max" << "\n";
        }

        void loadCallSiteData() {
          //TODO MATT: delete file
          std::ifstream InputFile("./_SD_CallSites.txt");
          std::string InputLine;
          std::string DebugLoc, ClassName, PreciseName;

          while (std::getline(InputFile, InputLine)) {
            std::stringstream LineStream(InputLine);

            std::getline(LineStream, DebugLoc, ',');
            std::getline(LineStream, ClassName, ',');
            LineStream >> PreciseName;
            CallSiteDebugLoc[DebugLoc] = ClassName;

            sdLog::stream() << DebugLoc << " is call to " << ClassName << ", " << PreciseName << "\n";
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

            sdLog::stream() << BaseClass << " subclass hierarchy loaded.\n";
          }
        }

        static MCSymbol *getLabelForGlobal(Twine globalName) {
          return Labels[globalName.str()];
        }

    private:
        std::map <std::string, std::string> CallSiteDebugLoc;
        std::map <std::string, std::vector<std::string>> ClassHierarchies;

        std::map <std::string, std::vector<MachineInstr *>> CallSiteMap;
        static std::map < std::string, std::pair<std::string, std::string> > RangeBounds;
        static std::map < std::string, MCSymbol*> Labels;

        static std::string debugLocToString(const DebugLoc &Log);
        static int count;
    };
}

#endif //LLVM_SAFEDISPATCHMACHINEFUNCION_H