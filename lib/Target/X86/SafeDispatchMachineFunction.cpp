#include "SafeDispatchMachineFunction.h"

using namespace llvm;

char SDMachineFunction::ID = 0;
INITIALIZE_PASS(SDMachineFunction, "sdMachinePass", "Get frontend infos.", false, false)

FunctionPass* llvm::createSDMachineFunctionPass() {
  return new SDMachineFunction();
}

