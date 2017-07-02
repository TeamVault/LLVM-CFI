#include "X86.h"
#include "X86MachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"

#include "llvm/Transforms/IPO/SafeDispatchReturnRange.h"
#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

using namespace llvm;

namespace {
    /**
     * This pass receives information generated in the SafeDispatch LTO passes
     * (SafeDispatchReturnRange) for use in the X86 backend.
     * */
    struct SDMachineFunction : public MachineFunctionPass {
    public:
        static char ID; // Pass identification, replacement for typeid

        SDMachineFunction() : MachineFunctionPass(ID) {
          sd_print("initializing SDMachineFunction pass\n");
          initializeSDMachineFunctionPass(*PassRegistry::getPassRegistry());
        }

        virtual ~SDMachineFunction() {
          sd_print("deleting SDMachineFunction pass\n");
        }

        bool runOnMachineFunction(MachineFunction &MF) override {
          //Matt: get the results from the LTO pass

          for (auto &element : MF) {
            for (auto &element2 : element) {
              if (element2.isCall())
                for (auto &MIop : element2.operands()) {
                  if (MIop.isGlobal()) {
                    auto vtableName = MIop.getGlobal()->getName().drop_front(3);
                    if (sd_isVtableName_ref(vtableName)) {
                      errs() << vtableName << "\n";
                    }
                  }
                }
            }
          }

          //TODO MATT: fix pass number
          sd_print("\n P??. Started running the backend pass (SDMachineFunction) ...\n");
          sd_print("\n P??. Finished running the backend pass (SDMachineFunction) ...\n");
          return true;
        }

    private:
    };
}

char SDMachineFunction::ID = 0;

INITIALIZE_PASS(SDMachineFunction, "sdMachinePass", "Get frontend infos.", false, false)

FunctionPass* llvm::createSDMachineFunctionPass() {
  return new SDMachineFunction();
}