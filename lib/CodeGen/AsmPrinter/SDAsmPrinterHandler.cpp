#include "SDAsmPrinterHandler.h"

#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/SafeDispatchMachineFunction.h"
#include "llvm/MC/MCStreamer.h"

using namespace llvm;

SDAsmPrinterHandler::SDAsmPrinterHandler(AsmPrinter *A):
        Asm(A), MMI(Asm->MMI) {}

SDAsmPrinterHandler::~SDAsmPrinterHandler() {}
