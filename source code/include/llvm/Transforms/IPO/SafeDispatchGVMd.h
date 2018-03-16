#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCHGVMD_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCHGVMD_H

#include <string>
#include "llvm/IR/Module.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Casting.h"
#include "llvm/IR/DebugInfoMetadata.h"

#include <utility>

/**
 * Helper function to create a metadata that contains the given integer
 *These functions are called from CGVTabless.cpp when generating the v tables.
 */
static llvm::Metadata* sd_getMDNumber(llvm::LLVMContext& C, uint64_t val) {
  return  llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(C), val));
}

/**
 * Helper function to create a metadata that contains the given string
 */
static llvm::Metadata* sd_getMDString(llvm::LLVMContext& C, const std::string& str) {
  return llvm::MDString::get(C, str.c_str());
}


/**
 * Gets a class name and returns a md that contains the vtable global variable
 */
static llvm::MDNode* sd_getClassVtblGVMD(const std::string& className, 
                                                const llvm::Module& M, 
                                 llvm::GlobalVariable* VTable = NULL) {
                                   
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
    llvm::Metadata* gvMd = llvm::ConstantAsMetadata::get(gv);
    assert(gvMd);
    return llvm::MDNode::get(C,gvMd);
  }
}

/**
 * Gets a class name and returns a tuple that contains the name and the vtable global variable
 * This is called from ItaniumCXXABI in oder to generate a MDNode containing the Context C,
 * the class name as MDNode and the class vtbl metadata. 
 */
static llvm::MDNode* sd_getClassNameMetadata(const std::string& className, 
                                             const llvm::Module& M, 
                                             llvm::GlobalVariable* VTable = NULL) {
  llvm::LLVMContext& C = M.getContext();
  llvm::MDNode* classNameMd = llvm::MDNode::get(C, llvm::MDString::get(C, className));
  llvm::MDNode* classVtblMd = sd_getClassVtblGVMD(className,M,VTable);
  return llvm::MDTuple::get(C, {classNameMd, classVtblMd});
}

typedef std::pair<llvm::MDNode*, llvm::MDNode*> sd_class_md_t;

/**
 * Gets a class name and returns a tuple that contains the name and the vtable global variable
 */
static sd_class_md_t sd_getClassNameMetadataPair(const std::string& className, 
                                                        const llvm::Module& M, 
                                        llvm::GlobalVariable* VTable = NULL) {
  llvm::LLVMContext& C = M.getContext();
  llvm::MDNode* classNameMd = llvm::MDNode::get(C, llvm::MDString::get(C, className));
  llvm::MDNode* classVtblMd = sd_getClassVtblGVMD(className,M,VTable);

  return sd_class_md_t(classNameMd, classVtblMd);
}

#endif

