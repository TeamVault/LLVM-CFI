#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/ADT/StringRef.h"
#include <vector>
#include "SafeDispatchMD.h"

namespace llvm {

#define STORE_OPCODE   28
#define GEP_OPCODE     29
#define BITCAST_OPCODE 44
#define CALL_OPCODE    49

  /**
   * Replaces each occurence of function "from" with function "to" inside the given module
   * @return whether there is made any replacement
   */
  bool sd_replaceCallFunctionWith(CallInst* from, Function* to, std::vector<Value*> args);

  /**
   * Replace the GEP's index value inside the given instruction
   */
  void sd_changeGEPIndex(GetElementPtrInst* inst, unsigned operandNo, int64_t newIndex);

  bool sd_isVTableName(StringRef& name);

  bool sd_isVTTName(StringRef& name);

} // End llvm namespace

#endif

