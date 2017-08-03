#include "SDAsmPrinterHandler.h"

#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/CodeGen/SafeDispatchMachineFunction.h"
#include "llvm/MC/MCStreamer.h"

using namespace llvm;

SDAsmPrinterHandler::SDAsmPrinterHandler(AsmPrinter *A):
        Asm(A), MMI(Asm->MMI) {}

SDAsmPrinterHandler::~SDAsmPrinterHandler() {}

bool SDAsmPrinterHandler::emitGlobalVariableInitializer(const GlobalVariable *GV) {
  if (!GV->getName().startswith("_SD_RANGE"))
    return false;

  MCSymbol *Label = SDMachineFunction::getLabelForGlobal(GV->getName());

  if (Label == nullptr) {
    errs()  << GV->getName() << ": no Label found!\n";
    return false;
  }

  //TODO MATT: Fix offset by calculating CallInst size (?)
  const MCExpr *LabelExpr = MCSymbolRefExpr::Create(Label, Asm->OutContext);
  Type *InitializerType = GV->getInitializer()->getType();
  uint64_t Size = GV->getParent()->getDataLayout().getTypeAllocSize(InitializerType);

  Asm->OutStreamer->EmitValue(LabelExpr, Size);
  errs() << GV->getName() << ": initializer is " << *Label << "\n";
  return true;
}
