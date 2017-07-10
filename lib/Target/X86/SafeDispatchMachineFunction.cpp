#include "SafeDispatchMachineFunction.h"

using namespace llvm;

char SDMachineFunction::ID = 0;
std::map <std::string, std::pair<std::string, std::string>> SDMachineFunction::RangeBounds;
int SDMachineFunction::count = 0;

INITIALIZE_PASS(SDMachineFunction, "sdMachinePass", "Get frontend infos.", false, true)

FunctionPass* llvm::createSDMachineFunctionPass() {
  return new SDMachineFunction();
}

std::string SDMachineFunction::debugLocToString(const DebugLoc &Log) {
  if (!Log)
    return "N/A";
  std::stringstream Stream;
  auto *Scope = cast<MDScope>(Log.getScope());
  Stream << Scope->getFilename().str() << ":" << Log.getLine() << ":" << Log.getCol();
  return Stream.str();
};
