#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCHVTBLMD_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCHVTBLMD_H

#include "CGCXXABI.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/VTableBuilder.h"
#include "llvm/Support/Casting.h"

#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"
#include "llvm/Transforms/IPO/SafeDispatchMD.h"

#include <iostream>
#include <string>
#include <vector>
#include <map>

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

namespace {
  /**
   * This class contains the information needed for each sub-vtable
   * to properly interleave them
   */
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

    void dump(std::ostream& out) {
      out << order << ", "
          << parentName << ", "
          << "[" << start << "," << end << "], "
          << addressPoint;
    }
  };
}

/**
 * Returns the mangled name of the given vtable symbol
 */
static std::string
sd_getClassName(clang::CodeGen::CGCXXABI* ABI, const clang::CXXRecordDecl *RD,
                const clang::BaseSubobject* Base) {
  assert(ABI && RD);

  if (Base) {
    return ABI->GetClassMangledConstrName(RD, *Base);
  } else {
    return ABI->GetClassMangledName(RD);
  }
}

/**
 * Helper function to extract the CXXRecordDecl from a QualType
 */
static const clang::CXXRecordDecl*
sd_getDeclFromQual(const clang::QualType& qt){
  const clang::Type* type = qt.getTypePtr();
  const clang::RecordType* recordType = llvm::dyn_cast<clang::RecordType>(type);
  assert(recordType);

  clang::RecordDecl* recordDecl = recordType->getDecl();
  clang::CXXRecordDecl* cxxRD = llvm::dyn_cast<clang::CXXRecordDecl>(recordDecl);
  assert(cxxRD);

  return cxxRD;
}

/**
 * Given a set of sub objects, figure out which one is the most derived one.
 *
 * Since we expect that given set of subobjects follow a inheritence chain,
 * we can "safely" return that is derived from all of them.
 */
static const clang::BaseSubobject*
sd_findMostDerived(std::set<const clang::BaseSubobject*>& objs) {
  auto itr = objs.begin();
  const clang::BaseSubobject* mostDerived = *itr;
  itr++;
  const clang::CXXRecordDecl* mostDerivedDecl = mostDerived->getBase();

  while(itr != objs.end()) {
    const clang::BaseSubobject* obj = *itr;
    itr++;
    const clang::CXXRecordDecl* objDecl = obj->getBase();

    if (! mostDerivedDecl->isDerivedFrom(objDecl) &&
        ! mostDerivedDecl->isVirtuallyDerivedFrom(objDecl)) {
      mostDerived = obj;
      mostDerivedDecl = objDecl;
    }
  }

  return mostDerived;
}

/**
 * Calculate the sub-vtable regions, their parent classes and their address points
 */
static std::vector<SD_VtableMD>
sd_generateSubvtableInfo(clang::CodeGen::CGCXXABI* ABI, const clang::VTableLayout* VTLayout,
                    const clang::CXXRecordDecl *RD, const clang::BaseSubobject* Base = NULL) {

  std::map<uint64_t, std::set<const clang::BaseSubobject*>> subObjMap;
  std::map<uint64_t, std::set<std::string>> addrPtMap;
  std::vector<SD_VtableMD> subVtables;

  std::string className = ABI->GetClassMangledName(RD);

  // order the sub-vtables according to their address points
  for (auto &&AP : VTLayout->getAddressPoints()) {
    const clang::BaseSubobject* subObj = &(AP.first);
    uint64_t addrPt = AP.second;

    // don't include the current classes' subobject
    subObjMap[addrPt].insert(subObj);
  }

  // find the most derived class for each address point
  for (auto a_itr = subObjMap.begin(); a_itr != subObjMap.end(); a_itr++) {
    uint64_t addrPt = a_itr->first;

    if (a_itr->second.size() == 1) {
      // if there is only one class in the address point, use it directly
      const clang::BaseSubobject* obj = *(a_itr->second.begin());
      addrPtMap[addrPt].insert(ABI->GetClassMangledName(obj->getBase()));
    } else {
      // look for the current class in the address point and delete it if found
      // this is needed since we want to use the direct parent if there is any
      for (const clang::BaseSubobject* obj : a_itr->second) {
        if (ABI->GetClassMangledName(obj->getBase()) == className) {
          a_itr->second.erase(obj);
          break;
        }
      }

      const clang::BaseSubobject* mostDerivedObj = sd_findMostDerived(a_itr->second);
      addrPtMap[addrPt].insert(ABI->GetClassMangledName(mostDerivedObj->getBase()));
    }
  }

  // hard part is over, now we simply mark the regions of each address point

  unsigned order   = 0; // order of the sub-vtable
  uint64_t start   = 0; // start of the current vtable
  uint64_t end     = 0; // end of the current vtable
  uint64_t prevVal = 0; // previous address point (for verify that std::map sorts the keys)

  uint64_t numComponents = VTLayout->getNumVTableComponents();

  // calculate the subvtable regions
  for (auto a_itr = addrPtMap.begin(); a_itr != addrPtMap.end(); a_itr++) {
    uint64_t addrPt  = a_itr->first;
    assert(prevVal < addrPt && "Address points are not sorted");
    assert(addrPtMap[addrPt].size() == 1 && "class has more than one direct class");
    assert(start < addrPt && "start exceeds address pointer");

    const std::string* parentName = &(*(addrPtMap[addrPt].begin()));

    end = addrPt;
    assert(end < numComponents);
    clang::VTableComponent::Kind kind = VTLayout->vtable_component_begin()[end].getKind();

    assert(kind == clang::VTableComponent::CK_FunctionPointer ||
           kind == clang::VTableComponent::CK_UnusedFunctionPointer ||
           kind == clang::VTableComponent::CK_CompleteDtorPointer ||
           kind == clang::VTableComponent::CK_DeletingDtorPointer);

    while (end + 1 < numComponents) {
      kind = VTLayout->vtable_component_begin()[end+1].getKind();
      if (kind == clang::VTableComponent::CK_FunctionPointer ||
          kind == clang::VTableComponent::CK_UnusedFunctionPointer ||
          kind == clang::VTableComponent::CK_CompleteDtorPointer ||
          kind == clang::VTableComponent::CK_DeletingDtorPointer) {
        end++;
      } else {
        break;
      }
    }

    if (*parentName == className) {
      subVtables.push_back(SD_VtableMD(order, "", start, end, addrPt));
    } else{
      subVtables.push_back(SD_VtableMD(order, *parentName, start, end, addrPt));
    }

    order++;
    start = end + 1;
    prevVal = addrPt;
  }

  if (start != VTLayout->getNumVTableComponents()) {
    sd_print("class:%s, start:%ld, total:%ld\n", className.c_str(), start, VTLayout->getNumVTableComponents());
    assert(false);
  }

  return subVtables;
}

/**
 * Given a vtable layout, insert a NamedMDNode that contains the information about the
 * vtable that is required for interleaving.
 */
static void
sd_insertVtableMD(clang::CodeGen::CodeGenModule* CGM, const clang::VTableLayout* VTLayout,
                    const clang::CXXRecordDecl *RD, const clang::BaseSubobject* Base = NULL) {
  assert(CGM && VTLayout && RD);

  clang::CodeGen::CGCXXABI* ABI = & CGM->getCXXABI();
  std::string className = sd_getClassName(ABI,RD,Base);

  // don't mess with C++ library classes, etc.
  if (! sd_isVtableName(className)) {
    return;
  }

  std::vector<SD_VtableMD> subVtables = sd_generateSubvtableInfo(ABI, VTLayout, RD, Base);

  // if we didn't produce anything, return ?
  if (subVtables.size() == 0) {
    return;
  }

  llvm::NamedMDNode* classInfo =
      CGM->getModule().getOrInsertNamedMetadata(SD_MD_CLASSINFO + className);

  if (classInfo->getNumOperands() > 0) {
    assert(classInfo->getNumOperands() == (2 + subVtables.size()));
    return;
  }

  llvm::LLVMContext& C = CGM->getLLVMContext();

  // first put the class name
  classInfo->addOperand(llvm::MDNode::get(C, sd_getMDString(C, className)));

  if (className == "_ZTVN11xercesc_2_510XMLDeleterE") {
    std::cerr << "subvtbl count: " << subVtables.size() << std::endl;
    for (unsigned i = 0; i < subVtables.size(); ++i) {
      subVtables[i].dump(std::cerr);
      std::cerr << std::endl;
    }
  }

  classInfo->addOperand(llvm::MDNode::get(C, sd_getMDNumber(C, subVtables.size())));

  // then add md for each sub-vtable
  for (unsigned i = 0; i < subVtables.size(); ++i) {
    classInfo->addOperand(subVtables[i].getMDNode(C));
  }
}

#endif

