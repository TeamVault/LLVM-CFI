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

namespace
{
typedef std::pair<std::string, uint64_t> vtbl_t;
typedef std::set<vtbl_t> vtbl_set_t;
typedef std::set<std::pair<std::string, uint64_t>> function_set_t;

/**
   * This class contains the information needed for each sub-vtable
   * to properly interleave them, the v table information is added
   * using the sd_insertVtableMD() function during compiler code 
   * generation phase and attached to a new node which will store this
   * additional information for the next passes.
   */
class SD_VtableMD
{
public:
  uint64_t order;
  const vtbl_set_t parents;
  uint64_t start;
  uint64_t end;
  uint64_t addressPoint;
  const function_set_t functions;

  //class constructor
  SD_VtableMD(uint64_t _order,
              const vtbl_set_t &_parents,
              uint64_t _start,
              uint64_t _end,
              uint64_t _addrPt,
              const function_set_t &_functions) : //this are initializers
                                  order(_order),
                                  parents(_parents),
                                  start(_start),
                                  end(_end),
                                  addressPoint(_addrPt),
                                  functions(_functions)
  {
  }

  //returns the MDNode
  llvm::MDNode *getMDNode(llvm::Module &M, llvm::LLVMContext &C)
  {
    std::vector<llvm::Metadata *> tuple;
    std::vector<llvm::Metadata *> parentsTuple;
    std::vector<llvm::Metadata *> functionsTuple;

    parentsTuple.push_back(sd_getMDNumber(C, parents.size()));

    for (auto it : parents)
    {
      parentsTuple.push_back(sd_getMDString(C, it.first));
      parentsTuple.push_back(sd_getMDNumber(C, it.second));
      parentsTuple.push_back(sd_getClassVtblGVMD(it.first, M));
    }

    functionsTuple.push_back(sd_getMDNumber(C, functions.size()));

    for (auto it : functions) {
      functionsTuple.push_back(sd_getMDString(C, it.first));
      functionsTuple.push_back(sd_getMDNumber(C, it.second));
    }

    tuple.push_back(sd_getMDNumber(C, order));
    tuple.push_back(sd_getMDNumber(C, start));
    tuple.push_back(sd_getMDNumber(C, end));
    tuple.push_back(sd_getMDNumber(C, addressPoint));
    tuple.push_back(llvm::MDNode::get(C, parentsTuple));
    tuple.push_back(llvm::MDNode::get(C, functionsTuple));

    return llvm::MDNode::get(C, tuple);
  }

  void dump(std::ostream &out)
  {
    out << "dump order: " << order << ", parents: "
        << parents.size() << ":{";

    for (auto pt : parents)
    {
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
static std::string sd_getClassName(clang::CodeGen::CGCXXABI *ABI,
                                   const clang::CXXRecordDecl *RD,
                                   const clang::BaseSubobject *Base)
{
  assert(ABI && RD);

  if (Base)
  {
    return ABI->GetClassMangledConstrName(RD, *Base);
  }
  else
  {
    return ABI->GetClassMangledName(RD);
  }
}


/**
 * Returns the mangled name of the given function symbol
 */
static std::string sd_getFunctionName(clang::CodeGen::CGCXXABI *ABI,
                                      const clang::CXXMethodDecl *MD)
{
  assert(ABI && MD);
  std::string functionName = ABI->GetFunctionMangledName(MD);
  return functionName;
}
/**
 * Helper function to extract the CXXRecordDecl from a QualType
 */
static const clang::CXXRecordDecl *sd_getDeclFromQual(const clang::QualType &qt)
{
  const clang::Type *type = qt.getTypePtr();
  const clang::RecordType *recordType = llvm::dyn_cast<clang::RecordType>(type);
  assert(recordType);

  clang::RecordDecl *recordDecl = recordType->getDecl();
  clang::CXXRecordDecl *cxxRD = llvm::dyn_cast<clang::CXXRecordDecl>(recordDecl);
  assert(cxxRD);

  return cxxRD;
}

/**
 * Given a set of sub objects, figure out which one is the most derived one.
 *
 * Since we expect that given set of subobjects follow an inheritence chain,
 * we can "safely" return that is derived from all of them.
 * This function is not used for now. 
 */
static const clang::BaseSubobject *sd_findMostDerived(std::set<const clang::BaseSubobject *> &objs)
{
  auto itr = objs.begin();
  const clang::BaseSubobject *mostDerived = *itr;
  //iterate forward
  itr++;
  //get the base class of the second object
  const clang::CXXRecordDecl *mostDerivedDecl = mostDerived->getBase();

  //iterate through all objects
  while (itr != objs.end())
  {
    const clang::BaseSubobject *obj = *itr;
    itr++;
    const clang::CXXRecordDecl *objDecl = obj->getBase();

    if (!mostDerivedDecl->isDerivedFrom(objDecl) &&
        !mostDerivedDecl->isVirtuallyDerivedFrom(objDecl))
    {
      mostDerived = obj;
      mostDerivedDecl = objDecl;
    }
  }

  return mostDerived;
}

/**
 * Calculate the sub-vtable regions, their parent classes and their address points.
 * It adds all the v tables and their ranges of the parent classes (most derived ones)
 * to the list of subVtables.
 */
static std::vector<SD_VtableMD> sd_generateSubvtableInfo(clang::CodeGen::CodeGenModule *CGM,
                                                         clang::CodeGen::CGCXXABI *ABI,
                                                         const clang::VTableLayout *VTLayout,
                                                         const clang::CXXRecordDecl *RD,
                                                         const clang::BaseSubobject *Base = NULL){

  //std::map<uint64_t, std::set<const clang::BaseSubobject*> > subObjMap;
  clang::ItaniumVTableContext &ctx = CGM->getVTables().getItaniumVTableContext();
  std::map<uint64_t, vtbl_set_t> addrPtMap;
  std::vector<SD_VtableMD> subVtables;
  unsigned order = 0; // order of the sub-vtable

  //std::cerr << "Emitting subvtable info for " << RD->getQualifiedNameAsString() << "\n";

  //print inheritance map and add the parent vtables to the addrPtMap
  //std::map<inheritance_path_t, uint64_t> = VTLayout->getInheritanceMap()
  for (auto it : VTLayout->getInheritanceMap())
  {

    //this is the address point for the given base class
    uint64_t addrPt = VTLayout->getAddressPoint(it.second);

    //declare a parent inheritance path and put first element
    clang::VTableLayout::inheritance_path_t parentInheritancePath(it.first);

    //get most derived class from this vtable layout and insert it to the inheritance path
    if (VTLayout->isConstructionLayout())
    {
      parentInheritancePath.insert(parentInheritancePath.begin(), VTLayout->getMostDerivedClass());
    }

    //printing the parent inherintance path for one v table layout if length > 0
    if (parentInheritancePath.size() > 0)
    {
      //std::cerr << "\n---Print an inheritance path with legth: " << parentInheritancePath.size() << "---\n";
      //std::cerr << "addrPt: " << addrPt << "-> has following inheritance chain: \n";
      //for (auto it1 : parentInheritancePath)
      //  std::cerr << "parent inheritance map element: " << it1->getQualifiedNameAsString() << ",\n";
    }
    
    //std::cerr << "\n (order in Inheritance Map " << it.second << ")\n";

    //declare an empty v table
    vtbl_t parentVtbl("", 0);

    //if there is a parent
    if (parentInheritancePath.size() > 0)
    {

      //from all the parenst pick the one from the front of the path
      const clang::CXXRecordDecl *DirectParent = parentInheritancePath.front();

      //erase the direct parent from inheritance path
      parentInheritancePath.erase(parentInheritancePath.begin());

      //if still there are more direct parents
      if (parentInheritancePath.size() > 0)
      {

        //get the layout of the direct parent
        const clang::VTableLayout &ParentLayout = ctx.getVTableLayout(DirectParent);

        //set the parent v table to be of the direct parent and the layout of this direct parent
        parentVtbl = vtbl_t(ABI->GetClassMangledName(DirectParent), ParentLayout.getOrder(parentInheritancePath));
      }
      else
      {
        //set parent v table, the order is now 0 since the parent inheritance path size is 0
        parentVtbl = vtbl_t(ABI->GetClassMangledName(DirectParent), 0);
      }
    }

    //std::cerr << "addr Pt:" << addrPt << " direct parent = " << parentVtbl.first << ", order: " << parentVtbl.second << "\n";

    // add the v table with order number 0 declared above
    //collect all the parent v tables
    addrPtMap[addrPt].insert(parentVtbl);
  }

  // TODO: Check that only the minium address point (if any) can have no
  // parent.

  /*
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
  */

  //start computing the ranges of the v table
  order = 0;
  uint64_t start = 0;   // start of the current vtable
  uint64_t end = 0;     // end of the current vtable
  uint64_t prevVal = 0; // previous address point (for verify that std::map sorts the keys)

  uint64_t numComponents = VTLayout->getNumVTableComponents();
  std::string className = ABI->GetClassMangledName(RD);

  // calculate the subvtable regions
  for (auto a_itr = addrPtMap.begin(); a_itr != addrPtMap.end(); a_itr++)
  {
    uint64_t addrPt = a_itr->first;
    assert(prevVal < addrPt && "Address points are not sorted");
    //assert(addrPtMap[addrPt].size() == 1 && "class has more than one direct class");
    assert(start < addrPt && "start exceeds address pointer");

    end = addrPt;
    assert(end < numComponents);
    clang::VTableComponent::Kind kind = VTLayout->vtable_component_begin()[end].getKind();

    function_set_t functions;

    //count the number of components in the v table
    while (end < numComponents)
    {
      auto component = VTLayout->vtable_component_begin()[end];
      auto kind = component.getKind();

      if (kind == clang::VTableComponent::CK_FunctionPointer ||
          kind == clang::VTableComponent::CK_UnusedFunctionPointer) {
        const clang::CXXMethodDecl *MD = component.getFunctionDecl();
        if (!clang::isa<clang::CXXConstructorDecl>(MD) && !clang::isa<clang::CXXDestructorDecl>(MD)) {
          std::string functionName = sd_getFunctionName(ABI, MD);
          functions.insert(std::pair<std::string, uint64_t>(sd_getFunctionName(ABI, MD), end - start));
        }
      }

      if (kind == clang::VTableComponent::CK_FunctionPointer ||
          kind == clang::VTableComponent::CK_UnusedFunctionPointer ||
          kind == clang::VTableComponent::CK_CompleteDtorPointer ||
          kind == clang::VTableComponent::CK_DeletingDtorPointer)
      {
        //count the number of the end
        end++;
      }
      else
      {
        break;
      }
    }

    for (auto it : addrPtMap[addrPt])
    {
      assert(it.first != className);
    }

    //create a new SD_VtableMD object and add it to the subVtables
    subVtables.push_back(SD_VtableMD(order, addrPtMap[addrPt], start, end - 1, addrPt, functions));
    order++;          //inc. order
    start = end;  //inc. start
    prevVal = addrPt; //set preVal
  }

  //after finishing witht the subVtables check that start != v table layout number of components
  if (start != VTLayout->getNumVTableComponents())
  {
    sd_print("class:%s, start:%ld, total:%ld \n", className.c_str(), start, VTLayout->getNumVTableComponents());
    assert(false);
  }

  return subVtables;
}

/**
 * Given a vtable layout, insert a NamedMDNode that contains the information about the
 * vtable that is required for interleaving. This function is called from CGVTables.cpp
 * locate in the code generation part of the compiler.
 * This information will be generated for each v table.
 * Each v table and its parents are added in the 
 * new node and added to the name metadata node SD_MD_CLASSINFO. 
 *
 * This function is called during code generation. Before any of our passes starts.
 */
static void sd_insertVtableMD(clang::CodeGen::CodeGenModule *CGM,
                              llvm::GlobalVariable *VTable,
                              const clang::VTableLayout *VTLayout,
                              const clang::CXXRecordDecl *RD,
                              const clang::BaseSubobject *Base = NULL)
{

  //std::cerr << " CGM: " << CGM << " VTLayout: " << VTLayout << " RD: " << RD << " RD->getQualifiedNameAsString() (class name): " << RD->getQualifiedNameAsString() << "\n";
  assert(CGM && VTLayout && RD);

  clang::CodeGen::CGCXXABI *ABI = &CGM->getCXXABI();

  //this is the class name in which we insert the new named meta data
  std::string className = sd_getClassName(ABI, RD, Base);

  // don't mess with C++ library classes, etc.
  if (!sd_isVtableName(className))
  {
    return;
  }

  //this generates the sub vtable info from the base and returns a vector of SD_VtableMD objects
  //all the v tables of the most derived parent classes of this class are added to the subVtables
  std::vector<SD_VtableMD> subVtables = sd_generateSubvtableInfo(CGM, ABI, VTLayout, RD, Base);

  // if we didn't produce anything, return ?
  if (subVtables.size() == 0)
  {
    return;
  }

  //our named metadata SD_MD_CLASSINFO will be inserted as a new NamedMDNode
  llvm::NamedMDNode *classInfo = CGM->getModule().getOrInsertNamedMetadata(SD_MD_CLASSINFO + className);

  // don't produce any duplicate md
  if (classInfo->getNumOperands() > 0)
  {
    std::set<uint64_t> aps;
    for (auto &&AP : VTLayout->getAddressPoints())
    {
      aps.insert(AP.second);
    }

    // last 1 is for class vtable
    //assert(classInfo->getNumOperands() == (2 + aps.size() + 1));
    return;
  }

  llvm::LLVMContext &C = CGM->getLLVMContext();

  llvm::Module &M = CGM->getModule();

  // first put the class name
  classInfo->addOperand(llvm::MDNode::get(C, sd_getMDString(C, className)));

  // second put the vtable global variable as a new MDNode into classInfo
  classInfo->addOperand(sd_getClassVtblGVMD(className, M, VTable));

  // third put the size of the tuple
  classInfo->addOperand(llvm::MDNode::get(C, sd_getMDNumber(C, subVtables.size())));

  // then add md for each sub-vtable
  for (unsigned i = 0; i < subVtables.size(); ++i)
  {
    classInfo->addOperand(subVtables[i].getMDNode(M, C));
  }

  // make sure parent class' metadata is added too
  for (auto &&AP : VTLayout->getAddressPoints())
  {
    const clang::BaseSubobject *subObj = &(AP.first);
    const clang::CXXRecordDecl *subRD = subObj->getBase();

    //do recursive call if subRD != RD "CXXRecordDecl"
    if (subRD != RD)
    {
      //std::cerr << "Recursively calling sd_insertVtableMD for " << subRD->getQualifiedNameAsString() << "\n";
      sd_insertVtableMD(CGM,
                        NULL,
                        &(CGM->getVTables().getItaniumVTableContext().getVTableLayout(subRD)),
                        subRD,
                        NULL);
    }
  }

  //save the v table layout inheritance paths
  //the path will help to check to which inheritance path a certain v table belongs
  //std::cerr << "Storing all inheritance paths contained in the VTableLayout \n";

  //store the number of paths
  //classInfo->addOperand(llvm::MDNode::get(C, sd_getMDNumber(C, VTLayout->getInheritanceMap().size())));
  for (auto it : VTLayout->getInheritanceMap())
  {

    //this is the address point for the given base class
    uint64_t addrPt = VTLayout->getAddressPoint(it.second);

    //declare a inheritance path and put first element
    clang::VTableLayout::inheritance_path_t inheritancePath(it.first);

    //store the legth of each path and then store the path
    //classInfo->addOperand(llvm::MDNode::get(C, sd_getMDNumber(C, inheritancePath.size())));
  }
}

#endif
