#ifndef LLVM_SAFEDISPATCHSYMBOLREPLACE_H
#define LLVM_SAFEDISPATCHSYMBOLREPLACE_H

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

#include "SafeDispatchMachineFunction.h"

#include <string>
#include <fstream>
#include <sstream>


namespace llvm {
    /**
     * This pass receives information generated in the SafeDispatch LTO passes
     * (SafeDispatchReturnRange) for use in the X86 backend.
     * */
    struct SDSymbolReplace : public MachineFunctionPass {
    public:
        static char ID; // Pass identification, replacement for typeid

        SDSymbolReplace() : MachineFunctionPass(ID) {
          sd_print("initializing SDSymbolReplace pass\n");
          initializeSDSymbolReplacePass(*PassRegistry::getPassRegistry());
        }

        virtual ~SDSymbolReplace() {
          sd_print("deleting SDSymbolReplace pass\n");
        }

        bool runOnMachineFunction(MachineFunction &MF) override {
          sd_print("P??. Started running SDSymbolReplace pass (%s) ...\n", MF.getName());

          for (auto &MBB: MF) {
            for (auto &MI : MBB) {
              int i = 0;
              for (auto &Op : MI.operands()) {
                if (Op.isGlobal() && Op.getGlobal()->getName().startswith("_SD_RANGESTUB_"))  {
                  errs() << "FOUND USAGE: " << MI << "\n";
                  Op.ChangeToMCSymbol(MF.getContext().GetOrCreateSymbol("SD_LABEL_0"));
                }
                i++;
              }
            }
          }
          sd_print("P??. Finished running SDMachineFunction pass (%s) ...\n", MF.getName());
          return false;
        }
    private:
    };
}

#endif //LLVM_SAFEDISPATCHSYMBOLREPLACE_H
