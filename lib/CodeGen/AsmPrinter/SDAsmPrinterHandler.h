//
// Created by matt on 7/29/17.
//
#ifndef LLVM_SDASMPRINTERHANDLER_H
#define LLVM_SDASMPRINTERHANDLER_H

#include "AsmPrinterHandler.h"
#include "llvm/ADT/DenseMap.h"

namespace llvm {

class MachineModuleInfo;
class MachineInstr;
class MachineFunction;
class AsmPrinter;
class MCSymbol;
class MCSymbolRefExpr;
class GlobalVariable;
class SDMachineFunction;

/// Emits exception handling directives.
class SDAsmPrinterHandler : public AsmPrinterHandler {
protected:
    /// Target of directive emission.
    AsmPrinter *Asm;

    /// Collected machine module information.
    MachineModuleInfo *MMI;

    /// SD Information.
    SDMachineFunction *SDInfo;

public:
    SDAsmPrinterHandler(AsmPrinter *A);
    ~SDAsmPrinterHandler() override;

    // Unused.
    void setSymbolSize(const MCSymbol *Sym, uint64_t Size) override {}
    void beginInstruction(const MachineInstr *MI) override {}
    void endInstruction() override {}
    void beginFunction(const MachineFunction *MF) override {}
    void endFunction(const MachineFunction *MF) override {}
    void endModule() override {};

    bool emitGlobalVariableInitializer(const GlobalVariable *GV) override;
};
}

#endif //LLVM_SDASMPRINTERHANDLER_H
