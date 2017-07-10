#ifndef LLVM_SAFEDISPATCHMACHINEFUNCION_H
#define LLVM_SAFEDISPATCHMACHINEFUNCION_H

#include "llvm/Transforms/IPO/SafeDispatchReturnRange.h"
#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "X86MachineFunctionInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Target/TargetInstrInfo.h"



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

        SDMachineFunction() : MachineFunctionPass(ID),
                              CallSiteDebugLoc(),
                              CallSiteMap() {
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

                // insert MI into the vectors for the base class and all of its subclasses!
                for (auto &SubClass : ClassHierarchies[className]) {
                  insert(SubClass, &MI);
                }

                sdLog::stream() << "\n";
                std::stringstream ss;
                ss << "SD_LABEL_" << count++;
                auto name = ss.str();

                auto data = MF.getMMI().getModule()->getGlobalVariable("_SD_RANGESTUB_ZTV1E_max");
                errs() << "Global Data: " << *data << "\n";

                auto symbol = MF.getContext().GetOrCreateSymbol(name);
                BuildMI(MBB, &MI, MI.getDebugLoc(), TII->get(TargetOpcode::GC_LABEL))
                        .addSym(symbol);

                //const MCSymbolRefExpr *FnExpr =
                 //       MCSymbolRefExpr::Create(FnSym, MCSymbolRefExpr::VK_PLT, Ctx);
                //EmitInstruction(Out, MCInstBuilder(X86::CALL64pcrel32).addExpr(FnExpr));


                //BuildMI(MBB, &MI, MI.getDebugLoc(), TII->get(X86::MOV))

              }
              //sdLog::stream() << "TESTING:  " << MI << "\n";
            }
          }

          //TODO MATT: fix pass number
          sd_print("P??. Finished running SDMachineFunction pass (%s) ...\n", MF.getName());
          for (auto &entry : RangeBounds) {
            auto bounds = entry.second;
            sdLog::stream() << entry.first
                            << ": (min: " << bounds.first
                            << " - max: " << bounds.second << ")\n";
          }

          return true;
        }

        void insert(std::string className, MachineInstr *MI) {
          sdLog::stream() << "Call is valid for vtable: " << className << "\n";
          CallSiteMap[className].push_back(MI);
          if (RangeBounds.find(className) == RangeBounds.end()) {
            RangeBounds[className].first = debugLocToString(MI->getDebugLoc());
          }
          RangeBounds[className].second = debugLocToString(MI->getDebugLoc());
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
            std::vector <std::string> SubClasses;
            while (std::getline(LineStream, Input, ',')) {
              SubClasses.push_back(Input);
            }

            ClassHierarchies[BaseClass] = SubClasses;

            sdLog::stream() << BaseClass << " subclass hierarchy loaded.\n";
          }
        }

    private:
        std::map <std::string, std::string> CallSiteDebugLoc;
        std::map <std::string, std::vector<std::string>> ClassHierarchies;

        std::map <std::string, std::vector<MachineInstr *>> CallSiteMap;
        static std::map <std::string, std::pair<std::string, std::string>> RangeBounds;

        static std::string debugLocToString(const DebugLoc &Log);
        static int count;
    };
}

#endif //LLVM_SAFEDISPATCHMACHINEFUNCION_H