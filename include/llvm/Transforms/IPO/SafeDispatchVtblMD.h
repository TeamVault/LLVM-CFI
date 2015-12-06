#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCHVTBLMD_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCHVTBLMD_H

#include "CGCXXABI.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/VTableBuilder.h"
#include "llvm/Support/Casting.h"

#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"
#include "llvm/Transforms/IPO/SafeDispatchMD.h"
#include "llvm/Transforms/IPO/SafeDispatchGVMd.h"

#include <iostream>
#include <string>
#include <vector>
#include <map>

namespace {
  typedef std::pair<std::string, uint64_t> vtbl_t;
  typedef std::set<vtbl_t> vtbl_set_t;

  /**
   * This class contains the information needed for each sub-vtable
   * to properly interleave them
   */
  class SD_VtableMD {
  public:
    uint64_t order;
    const vtbl_set_t parents;
    uint64_t start;
    uint64_t end;
    uint64_t addressPoint;

    SD_VtableMD(uint64_t _order, const vtbl_set_t &_parents,
                uint64_t _start, uint64_t _end, uint64_t _addrPt) :
      order(_order), parents(_parents), start(_start),
      end(_end), addressPoint(_addrPt) {}

    llvm::MDNode* getMDNode(llvm::Module& M, llvm::LLVMContext& C) {
      std::vector<llvm::Metadata*> tuple;
      std::vector<llvm::Metadata*> parentsTuple;

      parentsTuple.push_back(sd_getMDNumber(C, parents.size()));

      for (auto it: parents) {
        parentsTuple.push_back(sd_getMDString(C, it.first));
        parentsTuple.push_back(sd_getMDNumber(C, it.second));
        parentsTuple.push_back(sd_getClassVtblGVMD(it.first, M));
      }

      tuple.push_back(sd_getMDNumber(C, order));
      tuple.push_back(sd_getMDNumber(C, start));
      tuple.push_back(sd_getMDNumber(C, end));
      tuple.push_back(sd_getMDNumber(C, addressPoint));
      tuple.push_back(llvm::MDNode::get(C, parentsTuple));

      return llvm::MDNode::get(C, tuple);
    }

    void dump(std::ostream& out) {
      out << order << ", "
          << parents.size() << ":{";

      for (auto pt : parents) {
        out << "(" << pt.first << "," << pt.second << "),";
      }

      out << "}, "
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
sd_generateSubvtableInfo(clang::CodeGen::CodeGenModule* CGM,
                    clang::CodeGen::CGCXXABI* ABI, const clang::VTableLayout* VTLayout,
                    const clang::CXXRecordDecl *RD, const clang::BaseSubobject* Base = NULL) {

  //std::map<uint64_t, std::set<const clang::BaseSubobject*> > subObjMap;
  clang::ItaniumVTableContext &ctx = CGM->getVTables().getItaniumVTableContext();
  std::map<uint64_t, vtbl_set_t> addrPtMap;
  std::vector<SD_VtableMD> subVtables;
  unsigned order = 0; // order of the sub-vtable

  std::cerr << "Emitting subvtable info for " << RD->getQualifiedNameAsString() << "\n";

  clang::VTableLayout::parent_vector_t Parents = VTLayout->getParents();

  for (int i = 0; i < Parents.size(); i++) {
    uint64_t addrPt = VTLayout->getAddressPoint(i);
    vtbl_set_t s;

    if (Parents[i].directParents.size() == 0) {
      s.insert(vtbl_t("", 0));
    } else 
      for (auto it : Parents[i].directParents) {
        s.insert(vtbl_t(ABI->GetClassMangledName(it.first), it.second));
      }

    addrPtMap[addrPt] = s;
  }

  order = 0;
  uint64_t start   = 0; // start of the current vtable
  uint64_t end     = 0; // end of the current vtable
  uint64_t prevVal = 0; // previous address point (for verify that std::map sorts the keys)

  uint64_t numComponents = VTLayout->getNumVTableComponents();
  std::string className = ABI->GetClassMangledName(RD);

  // calculate the subvtable regions
  for (auto a_itr = addrPtMap.begin(); a_itr != addrPtMap.end(); a_itr++) {
    uint64_t addrPt  = a_itr->first;
    assert(prevVal < addrPt && "Address points are not sorted");
    //assert(addrPtMap[addrPt].size() == 1 && "class has more than one direct class");
    assert(start < addrPt && "start exceeds address pointer");

    end = addrPt;
    assert(end < numComponents);
    clang::VTableComponent::Kind kind = VTLayout->vtable_component_begin()[end].getKind();

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

    for (auto it : addrPtMap[addrPt]) {
      assert (it.first != className);
    }

    subVtables.push_back(SD_VtableMD(order, addrPtMap[addrPt], start, end, addrPt));
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
sd_insertVtableMD(clang::CodeGen::CodeGenModule* CGM, llvm::GlobalVariable* VTable,
                  const clang::VTableLayout* VTLayout, const clang::CXXRecordDecl *RD,
                  const clang::BaseSubobject* Base = NULL) {
  std::cerr << CGM << "," << VTLayout << "," << RD << "," << RD->getQualifiedNameAsString() <<"\n";
  assert(CGM && VTLayout && RD);

  clang::CodeGen::CGCXXABI* ABI = & CGM->getCXXABI();
  std::string className = sd_getClassName(ABI,RD,Base);

  // don't mess with C++ library classes, etc.
  if (!sd_isVtableName(className)) {
    return;
  }

  std::vector<SD_VtableMD> subVtables = sd_generateSubvtableInfo(CGM, ABI, VTLayout, RD, Base);

  // if we didn't produce anything, return ?
  if (subVtables.size() == 0) {
    return;
  }

  llvm::NamedMDNode* classInfo =
      CGM->getModule().getOrInsertNamedMetadata(SD_MD_CLASSINFO + className);

  // don't produce any duplicate md
  if (classInfo->getNumOperands() > 0) {
    std::set<uint64_t> aps;
    for (auto &&AP : VTLayout->getAddressPoints()) {
      aps.insert(AP.second);
    }
    // last 1 is for class vtable
    assert(classInfo->getNumOperands() == (2 + aps.size() + 1));
    return;
  }

  llvm::LLVMContext& C = CGM->getLLVMContext();

  llvm::Module& M = CGM->getModule();

  // first put the class name
  classInfo->addOperand(llvm::MDNode::get(C, sd_getMDString(C, className)));

  // second put the vtable global variable
  classInfo->addOperand(sd_getClassVtblGVMD(className,M,VTable));

  // third put the size of the tuple
  classInfo->addOperand(llvm::MDNode::get(C, sd_getMDNumber(C, subVtables.size())));

  // then add md for each sub-vtable
  for (unsigned i = 0; i < subVtables.size(); ++i) {
    classInfo->addOperand(subVtables[i].getMDNode(M,C));
  }

  // make sure parent class' metadata is added too
  for (auto &&AP : VTLayout->getAddressPoints()) {
    const clang::BaseSubobject* subObj = &(AP.first);
    const clang::CXXRecordDecl* subRD = subObj->getBase();

    if (subRD != RD) {
      std::cerr << "Recursively calling sd_insertVtableMD for " << subRD->getQualifiedNameAsString() << "\n";
      sd_insertVtableMD(CGM, NULL, &(CGM->getVTables().getItaniumVTableContext().getVTableLayout(subRD)),
                        subRD, NULL);
    }
  }
}

#endif

