#include "SDAsmPrinterHandler.h"

#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/CodeGen/SafeDispatchMachineFunction.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

using namespace llvm;

SDAsmPrinterHandler::SDAsmPrinterHandler(AsmPrinter *A):
        Asm(A), MMI(Asm->MMI) {}

SDAsmPrinterHandler::~SDAsmPrinterHandler() {}

bool SDAsmPrinterHandler::emitGlobalVariableInitializer(const GlobalVariable *GV) {
  return false;
}
