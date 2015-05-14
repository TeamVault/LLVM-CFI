#ifndef LLVM_TRANSFORMS_IPO_GENVTABLEMD_H
#define LLVM_TRANSFORMS_IPO_GENVTABLEMD_H

#include "CGCXXABI.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/VTableBuilder.h"
#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Support/Casting.h"

#include "llvm/Transforms/IPO/SafeDispatchMD.h"

#include <iostream>
#include <string>
#include <vector>
#include <map>

static llvm::MDNode*
sd_getMDNumber(llvm::LLVMContext& C, uint64_t val) {
  llvm::ConstantAsMetadata* cons =
      llvm::ConstantAsMetadata::get(
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(C), val));
  return llvm::MDNode::get(C, cons);
}

static llvm::MDNode*
sd_getMDString(llvm::LLVMContext& C, const std::string& str) {
  return llvm::MDNode::get(C, llvm::MDString::get(C, str.c_str()));
}

namespace {
  class SD_VtableMD {
  public:
    uint64_t order;
    const std::string parentName;
    uint64_t start;
    uint64_t end;
    uint64_t addressPoint;

    SD_VtableMD(uint64_t _order, const std::string& _parent, uint64_t _start,
                uint64_t _end, uint64_t _addrPt) :
      order(_order), parentName(_parent), start(_start),
      end(_end), addressPoint(_addrPt) {}

    llvm::MDNode* getMDNode(llvm::LLVMContext& C) {
      std::vector<llvm::Metadata*> tuple;
      tuple.push_back(sd_getMDNumber(C, order));
      tuple.push_back(sd_getMDString(C, parentName));
      tuple.push_back(sd_getMDNumber(C, start));
      tuple.push_back(sd_getMDNumber(C, end));
      tuple.push_back(sd_getMDNumber(C, addressPoint));

      return llvm::MDNode::get(C, tuple);
    }
  };
}

static std::string
sd_getClassName(clang::CodeGen::CGCXXABI* ABI, const clang::CXXRecordDecl *RD, const clang::BaseSubobject* Base) {
  assert(RD);

  if (Base) {
    return ABI->GetClassMangledConstrName(RD, *Base);
  } else {
    return ABI->GetClassMangledName(RD);
  }
}

static std::vector<SD_VtableMD>
sd_generateSubvtableInfo(clang::CodeGen::CGCXXABI* ABI, const clang::VTableLayout* VTLayout,
                    const clang::CXXRecordDecl *RD, const clang::BaseSubobject* Base = NULL) {

  std::string className = sd_getClassName(ABI,RD,Base);
  sd_print("%s's vtable:\n", className.c_str());

  std::map<uint64_t, std::set<std::string>> addrPtMap;
  std::set<std::string> baseClasses;
  std::vector<SD_VtableMD> subVtables;

  // find out the direct base classes
  for (const clang::CXXBaseSpecifier& Base : RD->bases()) {
    const clang::QualType& qt = Base.getType();
    const clang::Type* type = qt.getTypePtr();
    const clang::RecordType* recordType = llvm::dyn_cast<clang::RecordType>(type);
    assert(recordType);

    clang::RecordDecl* recordDecl = recordType->getDecl();
    clang::CXXRecordDecl* cxxRD = llvm::dyn_cast<clang::CXXRecordDecl>(recordDecl);
    assert(cxxRD);

    baseClasses.insert(ABI->GetClassMangledName(cxxRD).c_str());
  }

  // order the sub-vtables according to their address points
  for (auto &&AP : VTLayout->getAddressPoints()) {
    const clang::BaseSubobject* subObj = &(AP.first);
    uint64_t addrPt = AP.second;

    std::string subClassName = ABI->GetClassMangledName(subObj->getBase());

    if (baseClasses.find(subClassName) != baseClasses.end()) {
      addrPtMap[addrPt].insert(subClassName);
    }
  }

  // if this class doesn't inherit anything
  if (addrPtMap.empty()) {
    uint64_t addrPt = VTLayout->getAddressPoints().begin()->second;
    std::string parent = "";
    subVtables.push_back(SD_VtableMD((uint64_t) 0, parent, (uint64_t) 0,
                                     VTLayout->getNumVTableComponents()-1, addrPt));

    return subVtables;
  }


  unsigned order   = 0; // order of the sub-vtable
  uint64_t start   = 0; // start of the current vtable
  uint64_t end     = 0; // end of the current vtable
  uint64_t prevVal = 0; // previous address point (for verify that std::map sorts the keys)

  // calculate the subvtable regions
  for (auto a_itr = addrPtMap.begin(); a_itr != addrPtMap.end(); a_itr++) {
    uint64_t addrPt  = a_itr->first;
    assert(prevVal < addrPt && "Address points are not sorted");
    assert(addrPtMap[addrPt].size() == 1 && "class has more than one direct class");
    assert(start < addrPt && "start exceeds address pointer");

    const std::string* parentName = &(*(addrPtMap[addrPt].begin()));

    end = a_itr->first;

    clang::VTableComponent::Kind kind =
        VTLayout->vtable_component_begin()[end+1].getKind();
    while (kind == clang::VTableComponent::CK_FunctionPointer ||
           kind == clang::VTableComponent::CK_UnusedFunctionPointer ||
           kind == clang::VTableComponent::CK_CompleteDtorPointer ||
           kind == clang::VTableComponent::CK_DeletingDtorPointer) {
      kind = VTLayout->vtable_component_begin()[(++end) + 1].getKind();
    }

    subVtables.push_back(SD_VtableMD((uint64_t) 0, *parentName, start, end, addrPt));

    order++;
    start = end + 1;
    prevVal = addrPt;
  }

  return subVtables;
}

static void
sd_insertVtableMD(clang::CodeGen::CodeGenModule* CGM, const clang::VTableLayout* VTLayout,
                    const clang::CXXRecordDecl *RD, const clang::BaseSubobject* Base = NULL) {
  clang::CodeGen::CGCXXABI* ABI = & CGM->getCXXABI();
  std::vector<SD_VtableMD> subVtables = sd_generateSubvtableInfo(ABI, VTLayout, RD, Base);

  std::string className = sd_getClassName(ABI,RD,Base);
  llvm::NamedMDNode* classInfo =
      CGM->getModule().getOrInsertNamedMetadata(SD_MD_CLASSINFO(className));
  llvm::LLVMContext& C = CGM->getLLVMContext();

  classInfo->addOperand(sd_getMDString(C, className));        // 1. class name
  classInfo->addOperand(sd_getMDNumber(C,subVtables.size())); // 2. number of subvtables

  for (unsigned i = 0; i < subVtables.size(); ++i) {
    classInfo->addOperand(subVtables[i].getMDNode(C));
  }
}

#endif

