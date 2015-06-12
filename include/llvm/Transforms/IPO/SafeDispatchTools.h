#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_TOOLS_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_TOOLS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"

#include <string>

#include "SafeDispatchLog.h"

static bool sd_isVtableName_ref(const llvm::StringRef& name) {
  if (name.size() <= 4) {
    // name is too short, cannot be a vtable name
    return false;
  } else if (name.startswith("_ZTV") || name.startswith("_ZTC")) {
    llvm::StringRef rest = name.drop_front(4); // drop the _ZT(C|V) part

    return  (! rest.startswith("S")) &&      // but not from std namespace
        (! rest.startswith("N10__cxxabiv")); // or from __cxxabiv
  }

  return false;
}

static bool sd_isVtableName(std::string& className) {
  llvm::StringRef name(className);

  return sd_isVtableName_ref(name);
}

static bool sd_isVTTName(std::string& name) {
  return name.find("_ZTT") == 0;
}

static bool sd_isVthunk(const llvm::StringRef& name) {
  return name.startswith("_ZTv") || // virtual thunk
         name.startswith("_ZTcv");  // virtual covariant thunk
}

/**
 * Helper function to create a metadata that contains the given integer
 */
static llvm::Metadata*
sd_getMDNumber(llvm::LLVMContext& C, uint64_t val) {
  return  llvm::ConstantAsMetadata::get(
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(C), val));
}

/**
 * Helper function to create a metadata that contains the given string
 */
static llvm::Metadata*
sd_getMDString(llvm::LLVMContext& C, const std::string& str) {
  return llvm::MDString::get(C, str.c_str());
}

static void
sd_printVtable(llvm::GlobalVariable* globalVar) {
  llvm::StringRef varName = globalVar->getName();
  llvm::ConstantArray* vtable = llvm::dyn_cast<llvm::ConstantArray>(globalVar->getInitializer());
  assert(vtable);

  llvm::ConstantExpr* ce = NULL;
  llvm::ConstantInt* vtblInt = NULL;
  unsigned opcode = 0;

  sd_print("%s elements:\n", varName.bytes_begin());
  for (unsigned vtblInd = 0; vtblInd < vtable->getNumOperands(); vtblInd++) {
    ce = llvm::dyn_cast<llvm::ConstantExpr>(vtable->getOperand(vtblInd));
    opcode = ce ? ce->getOpcode() : 0;

    switch (opcode) {
      case llvm::Instruction::BitCast:
        sd_print("%-2u %s\n", vtblInd, ce->getOperand(0)->getName().bytes_begin());
        break;
      case llvm::Instruction::IntToPtr:
        vtblInt = llvm::dyn_cast<llvm::ConstantInt>(ce->getOperand(0));
        assert(vtblInt);
        sd_print("%-2u %ld\n", vtblInd, vtblInt->getSExtValue());
        break;
      default: // this must be a null value
        sd_print("%-2u 0\n", vtblInd);
        break;
    }
  }
}

#endif

