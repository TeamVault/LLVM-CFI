#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCHGVMD_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCHGVMD_H

#include <string>
#include "llvm/IR/Module.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Casting.h"

#include "SafeDispatchTools.h"

/**
 * Gets a class name and returns a md that contains the vtable global variable
 */
static llvm::MDNode*
sd_getClassVtblGVMD(const std::string& className, llvm::Module& M, llvm::GlobalVariable* VTable = NULL) {
  llvm::LLVMContext& C = M.getContext();
  llvm::GlobalVariable* gv = NULL;

  if (VTable) {
    gv = VTable;
  } else {
    gv = M.getGlobalVariable(className, true);
  }

  if (gv == NULL) {
    return llvm::MDNode::get(C,sd_getMDString(C, "NO_VTABLE"));
  } else {
    llvm::Metadata* gvMd = llvm::ConstantAsMetadata::getConstant(gv);
    assert(gvMd);
    return llvm::MDNode::get(C,gvMd);
  }
}

/**
 * Gets a class name and returns a tuple that contains the name and the vtable global variable
 */
static llvm::MDNode*
sd_getClassNameMetadata(const std::string& className, llvm::Module& M, llvm::GlobalVariable* VTable = NULL) {
  llvm::LLVMContext& C = M.getContext();
  llvm::MDNode* classNameMd = llvm::MDNode::get(C, llvm::MDString::get(C, className));
  llvm::MDNode* classVtblMd = sd_getClassVtblGVMD(className,M,VTable);
  return llvm::MDTuple::get(C, {classNameMd, classVtblMd});
}

#endif

