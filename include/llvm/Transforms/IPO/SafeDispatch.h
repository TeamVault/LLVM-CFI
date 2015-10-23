#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/ADT/StringRef.h"
#include <vector>
#include "SafeDispatchMD.h"

#define LOAD_OPCODE     27
#define STORE_OPCODE    28
#define GEP_OPCODE      29
#define INTTOPTR_OPCODE 43
#define BITCAST_OPCODE  44
#define ICMP_OPCODE     46
#define CALL_OPCODE     49
#define SELECT_OPCODE   50
#endif

