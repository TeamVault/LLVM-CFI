#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include <vector>

namespace llvm {

#define STORE_OPCODE   28
#define GEP_OPCODE     29
#define BITCAST_OPCODE 44
#define CALL_OPCODE    49

#define SD_DYNCAST_FUNC_NAME "__ivtbl_dynamic_cast"

  /**
   * Replaces each occurence of function "from" with function "to" inside the given module
   * @return whether there is made any replacement
   */
  bool replaceCallFunctionWith(CallInst* from, Function* to, std::vector<Value*> args);

  /**
   * Replace the GEP's index value inside the given instruction
   */
  void changeGEPIndex(Instruction* inst, unsigned operandNo, int64_t newIndex);

} // End llvm namespace

#endif

