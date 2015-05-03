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

#define SD_MD_CLASS_NAME "sd.class.name"
#define SD_MD_CAST_FROM  "sd.cast.from"
#define SD_MD_TYPEID     "sd.typeid"
#define SD_MD_VCALL      "sd.vcall"

  /**
   * Replaces each occurence of function "from" with function "to" inside the given module
   * @return whether there is made any replacement
   */
  bool sd_replaceCallFunctionWith(CallInst* from, Function* to, std::vector<Value*> args);

  /**
   * Replace the GEP's index value inside the given instruction
   */
  void sd_changeGEPIndex(GetElementPtrInst* inst, unsigned operandNo, int64_t newIndex);

} // End llvm namespace

#endif

