#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO/SafeDispatchCHA.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Transforms/IPO/SafeDispatchLog.h"

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>
#include <vector>
#include <set>
#include <map>
#include <math.h>
#include <algorithm>
#include <deque>

// you have to modify the following files for each additional LLVM pass
// 1. IPO.h and IPO.cpp
// 2. LinkAllPasses.h
// 3. InitializePasses.h

using namespace llvm;

#include <iostream>

char SDBuildCHA::ID = 0;

INITIALIZE_PASS(SDBuildCHA, "sdcha", "Build CHA pass for SafeDispatch", false, false)

ModulePass* llvm::createSDBuildCHAPass() {
  return new SDBuildCHA();
}

/**
 * Calculates the vtable order number given the index relative to
 * the beginning of the vtable
 */
unsigned SDBuildCHA::getVTableOrder(const vtbl_name_t& vtbl, uint64_t ind) {
  sd_print("Get vtable order %s ind %d\n", vtbl.c_str(), ind);
  assert(rangeMap.find(vtbl) != rangeMap.end());

  std::vector<range_t>& ranges = rangeMap[vtbl];
  for (int i = 0; i < ranges.size(); i++) {
    if (ranges[i].first <= ind && 
        ranges[i].second >= ind)
      return i;
  }

  sd_print("Index %d is not in any range for %s\n", ind, vtbl.c_str());
  assert(false && "Index not in range");
}



void SDBuildCHA::preorderHelper(std::vector<SDBuildCHA::vtbl_t>& nodes, const SDBuildCHA::vtbl_t& root){
  nodes.push_back(root);
  if (cloudMap.find(root) != cloudMap.end()) {
    for (const SDBuildCHA::vtbl_t& n : cloudMap[root]) {
      preorderHelper(nodes, n);
    }
  }
}

std::vector<SDBuildCHA::vtbl_t> SDBuildCHA::preorder(const vtbl_t& root) {
  order_t nodes;
  preorderHelper(nodes, root);
  return nodes;
}

static inline uint64_t
sd_getNumberFromMDTuple(const MDOperand& op) {
  Metadata* md = op.get();
  assert(md);
  ConstantAsMetadata* cam = dyn_cast_or_null<ConstantAsMetadata>(md);
  assert(cam);
  ConstantInt* ci = dyn_cast<ConstantInt>(cam->getValue());
  assert(ci);

  return ci->getSExtValue();
}

static inline SDBuildCHA::vtbl_name_t
sd_getStringFromMDTuple(const MDOperand& op) {
  MDString* mds = dyn_cast_or_null<MDString>(op.get());
  assert(mds);

  return mds->getString().str();
}

void SDBuildCHA::buildClouds(Module &M) {
  // this set is used for checking if a parent class is defined or not
  std::set<vtbl_name_t> build_undefinedVtables;

  for(auto itr = M.getNamedMDList().begin(); itr != M.getNamedMDList().end(); itr++) {
    NamedMDNode* md = itr;

    // check if this is a metadata that we've added
    if(! md->getName().startswith(SD_MD_CLASSINFO))
      continue;

    sd_print("GOT METADATA: %s\n", md->getName().data());

    std::vector<nmd_t> infoVec = extractMetadata(md);

    for (const nmd_t& info : infoVec) {
      // record the old vtable array
      GlobalVariable* oldVtable = M.getGlobalVariable(info.className, true);

      sd_print("class %s with %d subtables\n", info.className.c_str(), info.subVTables.size());

      sd_print("oldvtables: %p, %d, class %s\n",
               oldVtable,
               oldVtable ? oldVtable->hasInitializer() : -1,
               info.className.c_str());

      if (oldVtable && oldVtable->hasInitializer()) {
        ConstantArray* vtable = dyn_cast<ConstantArray>(oldVtable->getInitializer());
        assert(vtable);
        oldVTables[info.className] = vtable;
      } else {
        undefinedVTables.insert(info.className);
      }

      for(unsigned ind = 0; ind < info.subVTables.size(); ind++) {
        const nmd_sub_t* subInfo = & info.subVTables[ind];
        vtbl_t name(info.className, ind);
        sd_print("SubVtable[%d] Order: %d Parent: %s [%d-%d] AddrPt: %d\n",
          ind, 
          subInfo->order,
          subInfo->parentName.c_str(),
          subInfo->start,
          subInfo->end,
          subInfo->addressPoint);

        if (ind == 0) {
          // remove the primary vtable from the build_undefined vtables map
          if (build_undefinedVtables.find(info.className) != build_undefinedVtables.end()) {
            sd_print("Removing %s from build_udnefinedVtables\n", info.className.c_str());
            build_undefinedVtables.erase(info.className);
          }

          if (cloudMap.find(name) == cloudMap.end()){
            sd_print("Inserting %s, %d in cloudMap ind 0\n", name.first.c_str(), name.second);
            cloudMap[name] = std::set<vtbl_t>();
          }
        }

        if (subInfo->parentName != "") {
          vtbl_t parent(subInfo->parentName, 0);

          // if the parent class is not defined yet, add it to the
          // undefined vtable set
          if (cloudMap.find(parent) == cloudMap.end()) {
            sd_print("Inserting %s, %d in cloudMap - undefined parent\n", name.first.c_str(), name.second);
            cloudMap[parent] = std::set<vtbl_t>();
            sd_print("Adding %s to build_udnefinedVtables\n", subInfo->parentName.c_str());
            build_undefinedVtables.insert(subInfo->parentName);
          }

          // add the current class to the parent's children set
          cloudMap[parent].insert(name);
        } else {
          assert(ind == 0); // make sure secondary vtables have a direct parent
          // add the class to the root set
          roots.insert(info.className);
        }

        // record the original address points
        addrPtMap[info.className].push_back(subInfo->addressPoint);

        // record the sub-vtable ends
        rangeMap[info.className].push_back(range_t(subInfo->start, subInfo->end));

        // record the class name of the sub-object
        subObjNameMap[info.className].push_back(subInfo->parentName);
      }
    }
  }

  if (build_undefinedVtables.size() != 0) {
    sd_print("Undefined vtables:\n");
    for (auto n : build_undefinedVtables) {
      sd_print("%s\n", n.c_str());
    }
  }
  assert(build_undefinedVtables.size() == 0);
}

static llvm::GlobalVariable* sd_mdnodeToGV(Metadata* vtblMd) {
  llvm::MDNode* mdNode = dyn_cast<llvm::MDNode>(vtblMd);
  assert(mdNode);
  Metadata* md = mdNode->getOperand(0).get();

  if(!md) {
    return NULL;
  }

  if(dyn_cast<llvm::MDString>(md)) {
    return NULL;
  }

  llvm::ConstantAsMetadata* vtblCAM = dyn_cast_or_null<ConstantAsMetadata>(md);
  if(! vtblCAM) {
    md->dump();
    assert(false);
  }
  Constant* vtblC = vtblCAM->getValue();
  GlobalVariable* vtblGV = dyn_cast<GlobalVariable>(vtblC);
  assert(vtblGV);
  return vtblGV;
}

std::vector<SDBuildCHA::nmd_t>
SDBuildCHA::extractMetadata(NamedMDNode* md) {
  std::set<vtbl_name_t> classes;
  std::vector<SDBuildCHA::nmd_t> infoVec;

  unsigned op = 0;

  do {
    SDBuildCHA::nmd_t info;
    MDString* infoMDstr = dyn_cast_or_null<MDString>(md->getOperand(op++)->getOperand(0));
    assert(infoMDstr);
    info.className = infoMDstr->getString().str();
    GlobalVariable* classVtbl = sd_mdnodeToGV(md->getOperand(op++));

    if (classVtbl) {
      info.className = classVtbl->getName();
    }

    unsigned numOperands = sd_getNumberFromMDTuple(md->getOperand(op++)->getOperand(0));

    for (unsigned i = op; i < op + numOperands; ++i) {
      SDBuildCHA::nmd_sub_t subInfo;
      llvm::MDTuple* tup = dyn_cast<llvm::MDTuple>(md->getOperand(i));
      assert(tup);
      assert(tup->getNumOperands() == 6);
//      if (tup->getNumOperands() != 6) {
//        sd_print("node operand count: %u\n", md->getNumOperands());
//        sd_print("tuple operand count: %u\n", tup->getNumOperands());
//        tup->dump();
//        assert(false);
//      }

      subInfo.order = sd_getNumberFromMDTuple(tup->getOperand(0));
      subInfo.parentName = sd_getStringFromMDTuple(tup->getOperand(1));

      GlobalVariable* parentVtable = sd_mdnodeToGV(tup->getOperand(2).get());

      if(parentVtable) {
        subInfo.parentName = parentVtable->getName();
      }

      subInfo.start = sd_getNumberFromMDTuple(tup->getOperand(3));
      subInfo.end = sd_getNumberFromMDTuple(tup->getOperand(4));
      subInfo.addressPoint = sd_getNumberFromMDTuple(tup->getOperand(5));


      bool currRangeCheck = (subInfo.start <= subInfo.addressPoint &&
                     subInfo.addressPoint <= subInfo.end);
      bool prevVtblCheck = (i == op || (--info.subVTables.end())->end < subInfo.start);

//      sd_print("%lu, %s, %lu, %lu, %lu\n", subInfo.order, subInfo.parentName.c_str(),
//               subInfo.start, subInfo.end, subInfo.addressPoint);

      assert(currRangeCheck && prevVtblCheck);

      info.subVTables.push_back(subInfo);
    }
    op += numOperands;

    if (classes.count(info.className) == 0) {
      classes.insert(info.className);
      infoVec.push_back(info);
    }
  } while (op < md->getNumOperands());

  return infoVec;
}

int64_t SDBuildCHA::getCloudSize(const SDBuildCHA::vtbl_name_t& vtbl) {
  vtbl_t v(vtbl, 0);
  return cloudSizeMap[v];
}

uint32_t SDBuildCHA::calculateChildrenCounts(const SDBuildCHA::vtbl_t& root){
  uint32_t count = isDefined(root) ? 1 : 0;
  if (cloudMap.find(root) != cloudMap.end()) {
    for (const SDBuildCHA::vtbl_t& n : cloudMap[root]) {
      count += calculateChildrenCounts(n);
    }
  }

  assert(cloudSizeMap.find(root) == cloudSizeMap.end());
  cloudSizeMap[root] = count;

  return count;
}

void SDBuildCHA::clearAnalysisResults() {
  cloudMap.clear();
  roots.clear();
  addrPtMap.clear();
  rangeMap.clear();
  ancestorMap.clear();
  oldVTables.clear();
  cloudSizeMap.clear();

  sd_print("Cleared SDBuildCHA analysis results\n");
}

/// ----------------------------------------------------------------------------
/// Helper functions
/// ----------------------------------------------------------------------------

void SDBuildCHA::printClouds() {
  int rc = system("rm -rf /tmp/dot && mkdir /tmp/dot");
  assert(rc == 0);

  for(const vtbl_name_t& rootName : roots) {
    assert(rootName.length() <= 490);

    char filename[512];
    sprintf(filename, "/tmp/dot/%s.dot", rootName.data());

    FILE* file = fopen(filename, "w");
    assert(file);

    fprintf(file, "digraph %s {\n", rootName.data());

    vtbl_t root(rootName,0);

    std::deque<vtbl_t> classes;
    classes.push_back(root);

    while(! classes.empty()) {
      vtbl_t vtbl = classes.front();
      fprintf(file, "\t \"(%s,%lu)\";\n", vtbl.first.data(), vtbl.second);
      classes.pop_front();

      for (const vtbl_t& child : cloudMap[vtbl]) {
        fprintf(file, "\t \"(%s,%lu)\" -> \"(%s,%lu)\";\n",
                vtbl.first.data(), vtbl.second,
                child.first.data(), child.second);
        classes.push_back(child);
      }
    }

    fprintf(file, "}\n");
    fclose(file);
  }
}

SDBuildCHA::vtbl_t SDBuildCHA::getFirstDefinedChild(const vtbl_t &vtbl) {
  assert(isUndefined(vtbl));
  order_t const &order = preorder(vtbl);

  for (const vtbl_t& c : order) {
    if (c != vtbl && isDefined(c))
      return c;
  }

  // If we get here then there is an undefined class with no
  // defined subclasses.
  std::cerr << vtbl.first << "," << vtbl.second << " doesn't have first defined child\n";
  for (const vtbl_t& c : order) {
    std::cerr << c.first << "," << c.second << " isn't defined\n";
  }
  assert(false); // unreachable
}

bool SDBuildCHA::hasFirstDefinedChild(const vtbl_t &vtbl) {
  //assert(isUndefined(vtbl));
  order_t const &order = preorder(vtbl);

  for (const vtbl_t& c : order) {
    if (c != vtbl && isDefined(c))
      return true;
  }

  return false;
}

bool SDBuildCHA::knowsAbout(const vtbl_t &vtbl) {
  return cloudMap.find(vtbl) != cloudMap.end();
}

int64_t SDBuildCHA::getSubVTableIndex(const vtbl_name_t& derived,
                                       const vtbl_name_t &base) {
  
  for (int64_t ind = 0; ind < subObjNameMap[derived].size(); ind++) {
    std::cerr << subObjNameMap[derived][ind] << "\n";
    if (subObjNameMap[derived][ind] == base)
      return ind;
  }
  return -1;
}

bool SDBuildCHA::hasAncestor(const vtbl_t &vtbl) {
  return ancestorMap.find(vtbl) != ancestorMap.end();
}

SDBuildCHA::vtbl_name_t SDBuildCHA::getAncestor(const vtbl_t &vtbl) {
  return ancestorMap[vtbl];
}
