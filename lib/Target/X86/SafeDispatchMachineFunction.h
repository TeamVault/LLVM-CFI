#ifndef LLVM_SAFEDISPATCHMACHINEFUNCION_H
#define LLVM_SAFEDISPATCHMACHINEFUNCION_H


#include "X86.h"
#include "X86MachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/CodeGen/MachineModuleInfo.h"

#include "llvm/Transforms/IPO/SafeDispatchReturnRange.h"
#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

#include <string>
#include <fstream>
#include <sstream>


namespace llvm {
    /**
     * This pass receives information generated in the SafeDispatch LTO passes
     * (SafeDispatchReturnRange) for use in the X86 backend.
     * */
    struct SDMachineFunction : public MachineFunctionPass {
    public:
        static char ID; // Pass identification, replacement for typeid

        SDMachineFunction() : MachineFunctionPass(ID), CallSiteDebugLoc(), CallSiteMap() {
          sd_print("initializing SDMachineFunction pass\n");
          initializeSDMachineFunctionPass(*PassRegistry::getPassRegistry());

          loadCallSiteData();
          loadCallHierarchyData();
        }

        virtual ~SDMachineFunction() {
          sd_print("deleting SDMachineFunction pass\n");
        }


        bool runOnMachineFunction(MachineFunction &MF) override {
          sd_print("P??. Started running SDMachineFunction pass (%s) ...\n", MF.getName());

          //We would get NamedMetadata like this:
          //const auto &M = MF.getMMI().getModule();
          //const auto &MD = M->getNamedMetadata("sd.class_info._ZTV1A");
          //MD->dump();

          for (auto &MBB: MF) {
            for (auto &MI : MBB) {
              if (MI.isCall()) {
                auto debugLocString = debugLocToString(MI.getDebugLoc());

                auto classNameIt = CallSiteDebugLoc.find(debugLocString);
                if(classNameIt == CallSiteDebugLoc.end())
                  continue;

                auto className = classNameIt->second;
                sdLog::stream() << "Machine CallInst:\n" << MI
                                << "has callee base class: " << className << "\n";

                // insert MI into the vectors for the base class and all of its subclasses!
                CallSiteMap[className].push_back(&MI);
                sdLog::stream() << "Call is valid for Class: " << className << "\n";
                for (auto &SubClass : ClassHierarchies[className]) {
                  CallSiteMap[SubClass].push_back(&MI);
                  sdLog::stream() << "Call is valid for Subclass: " << SubClass << "\n";
                }
                sdLog::stream() << "\n";
              }
            }
          }

          //TODO MATT: fix pass number
          sd_print("P??. Finished running SDMachineFunction pass (%s) ...\n", MF.getName());
          return true;
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
            CallSiteDebugLoc[DebugLoc] = PreciseName;

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
            std::vector<std::string> SubClasses;
            while (std::getline(LineStream, Input, ',')) {
              SubClasses.push_back(Input);
            }

            ClassHierarchies[BaseClass] = SubClasses;

            sdLog::stream() << BaseClass << " subclass hierarchy loaded.\n";
          }
        }

    private:
        std::map<std::string, std::string> CallSiteDebugLoc;
        std::map<std::string, std::vector<std::string>> ClassHierarchies;

        std::map<std::string, std::vector<MachineInstr*>> CallSiteMap;

        //TODO: move shitty helper somewhere else or fix DebugLoc::print not working
        std::string debugLocToString(const DebugLoc &Log) {
          std::stringstream Stream;
          auto *Scope = cast<MDScope>(Log.getScope());
          Stream << Scope->getFilename().str() << ":" << Log.getLine() << ":" << Log.getCol();
          return Stream.str();
        };

    };
}

#endif //LLVM_SAFEDISPATCHMACHINEFUNCION_H