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
  assert(rangeMap.find(vtbl) != rangeMap.end());

  std::vector<range_t>& ranges = rangeMap[vtbl];
  for (int i = 0; i < ranges.size(); i++) {
    if (ranges[i].first <= ind && ranges[i].second >= ind) //Paul: if first is less than ind and second is greather than ind
      return i;
  }

  sd_print("Index %d is not in any range for %s\n", ind, vtbl.c_str());
  assert(false && "Index not in range");
}



void 
SDBuildCHA::preorderHelper(std::vector<SDBuildCHA::vtbl_t>& nodes, const SDBuildCHA::vtbl_t& root, vtbl_set_t &visited) {
  if (visited.find(root) != visited.end())
    return;

  nodes.push_back(root);
  visited.insert(root);

  if (cloudMap.find(root) != cloudMap.end()) { //Paul: check that cloudmap is not empty
    for (const SDBuildCHA::vtbl_t& n : cloudMap[root]) {
      preorderHelper(nodes, n, visited);
    }
  }
}

std::vector<SDBuildCHA::vtbl_t> 
SDBuildCHA::preorder(const vtbl_t& root) {
  order_t nodes;
  vtbl_set_t visited;
  preorderHelper(nodes, root, visited);
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

void SDBuildCHA::verifyClouds(Module &M) {
  for (auto rootName : roots) {
    vtbl_t root(rootName, 0);
    assert(cloudMap.count(root)); 
  }
}

SDBuildCHA::vtbl_t 
SDBuildCHA::findLeastCommonAncestor(const SDBuildCHA::vtbl_set_t &vtbls, SDBuildCHA::cloud_map_t &ptMap) {

 cloud_map_t ancestorsMap;

  for (auto vtbl : vtbls) {
    std::vector<vtbl_t> q;
    q.push_back(vtbl);

    while (q.size() > 0) {
      vtbl_t cur = q.back();
      q.pop_back();

      if (ancestorsMap[vtbl].find(cur) == ancestorsMap[vtbl].end()) {
        ancestorsMap[vtbl].insert(cur);
        if (ptMap.find(cur) != ptMap.end())
          for (auto pt : ptMap[cur])  q.push_back(pt);
      }
    }
  }
  
  /**Paul: this a great place to improve. Their implementation is not optimal.
  */
  // TODO(dbounov) The below algorithm is a conservative
  // heuristic. The actual problem to solve is the "lowest"
  // node in the CHA that intercepts all paths leading up to the root.
  // The current implementation just finds the topmost common ancestor.
  vtbl_t candidate(ancestorMap[*vtbls.begin()], 0);
  do {
    vtbl_t nextCandidate;
    int nChildrenCommonAncestors = 0;

    // Count the number of children of the current candidate
    // that are also common ancestors
    for (auto child : cloudMap[candidate]) {
      int nDescendents = 0;
      for (auto it : ancestorsMap)
        if (it.second.find(child) != it.second.end()) nDescendents++;

      if (nDescendents == vtbls.size()) {
        nextCandidate = child;
        nChildrenCommonAncestors++;
      }
    }

    // If there is not a single common ancestor amongst the candidate's
    // children conservatively terminate search
    if (nChildrenCommonAncestors != 1)
      break;

    candidate = nextCandidate;
  } while (1); //Paul: run once

  return candidate;
}

/*Paul:
this is the main method here. This method builds the cloud map */
void SDBuildCHA::buildClouds(Module &M) {
  // this set is used for checking if a parent class is defined or not
  std::set<vtbl_t> build_undefinedVtables;

  for(auto itr = M.getNamedMDList().begin(); itr != M.getNamedMDList().end(); itr++) {
    NamedMDNode* md = itr;

    // check if this is a metadata that we've added
    if(! md->getName().startswith(SD_MD_CLASSINFO))
      continue;

    //sd_print("GOT METADATA: %s\n", md->getName().data());

    std::vector<nmd_t> infoVec = extractMetadata(md);

    for (const nmd_t& info : infoVec) {
      // record the old vtable array
      /* Paul:
      this GlobalVariable holds the metadata for each module.
      Inside the metadata the v tables are contained.
      */
      GlobalVariable* oldVtable = M.getGlobalVariable(info.className, true);

      //sd_print("class %s with %d subtables\n", info.className.c_str(), info.subVTables.size());

      /*
      sd_print("oldvtables: %p, %d, class %s\n",
               oldVtable,
               oldVtable ? oldVtable->hasInitializer() : -1,
               info.className.c_str());
      */

      if (oldVtable && oldVtable->hasInitializer()) {
        ConstantArray* vtable = dyn_cast<ConstantArray>(oldVtable->getInitializer());
        assert(vtable);
        oldVTables[info.className] = vtable;
      } else {
        undefinedVTables.insert(info.className);
      }
      
      //Paul: iterate trough the sub v tables of the metadata vector
      for(unsigned ind = 0; ind < info.subVTables.size(); ind++) {
        const nmd_sub_t* subInfo = & info.subVTables[ind];
        vtbl_t name(info.className, ind);
        /*
        sd_print("SubVtable[%d] Order: %d Parents[%d]: ",
          ind, 
          subInfo->order,
          subInfo->parents.size());

        for (auto it : subInfo->parents) {
          sd_print("(%s,%d),", it.first.c_str(), it.second);
        }

        sd_print(" [%d-%d] AddrPt: %d\n",
          subInfo->start,
          subInfo->end,
          subInfo->addressPoint);
        */

        if (build_undefinedVtables.find(name) != build_undefinedVtables.end()) {
          //sd_print("Removing %s,%d from build_udnefinedVtables\n", name.first.c_str(), name.second);
          build_undefinedVtables.erase(name);
        }

        if (cloudMap.find(name) == cloudMap.end()){
          //sd_print("Inserting %s, %d in cloudMap\n", name.first.c_str(), name.second);
          cloudMap[name] = std::set<vtbl_t>();
        }

        vtbl_set_t parents;
        //Paul: interate now through each subinfo and get the parents
        for (auto it : subInfo->parents) {
          if (it.first != "") {
            vtbl_t &parent = it;
            parents.insert(parent);

            // if the parent class is not defined yet, add it to the
            // undefined vtable set
            if (cloudMap.find(parent) == cloudMap.end()) {
              //sd_print("Inserting %s, %d in cloudMap - undefined parent\n", parent.first.c_str(), parent.second);
              cloudMap[parent] = std::set<vtbl_t>();
              build_undefinedVtables.insert(parent);
            }

            // add the current class to the parent's children set
            cloudMap[parent].insert(name);
          } else {
            assert(ind == 0); // make sure secondary vtables have a direct parent
            // add the class to the root set
            roots.insert(info.className);
          }
        }
        
        // Paul: record the parrents for each class 
        parentMap[info.className].push_back(parents);

        // record the original address points
        addrPtMap[info.className].push_back(subInfo->addressPoint);

        // record the sub-vtable ends
        rangeMap[info.className].push_back(range_t(subInfo->start, subInfo->end));
      }
    }
  }

  if (build_undefinedVtables.size() != 0) {
    sd_print("Build Undefined vtables:\n");
    for (auto n : build_undefinedVtables) {
      sd_print("%s,%d\n", n.first.c_str(), n.second);
    }
  }

  assert(build_undefinedVtables.size() == 0);

  for (auto rootName : roots) {
    vtbl_t root(rootName, 0);
    for (auto child : preorder(root)) {
      if (ancestorMap.find(child) == ancestorMap.end()) {
        ancestorMap[child] = rootName;
      }
    }
  }

  for (auto it : parentMap) {
    const vtbl_name_t &className = it.first;
    const std::vector<vtbl_set_t> &parentSetV = it.second;

    std::cerr << "(" << className << ",[";

    for (int ind = 0; ind < parentSetV.size(); ind++) {
      std::cerr << "{";
      for (auto ptIt : parentSetV[ind])
        std::cerr << "<" << ptIt.first << "," << ptIt.second << ">,";
      std::cerr << "},";
    }

    std::cerr << "]\n";
  }

  for (auto it : parentMap) {
    const vtbl_name_t &className = it.first;
    const std::vector<vtbl_set_t> &parentSetV = it.second;

    for (int ind = 0; ind < parentSetV.size(); ind++) {
      vtbl_name_t layoutClass = "none";

      // Check that all possible parents are in the same layout cloud
      for (auto ptIt : parentSetV[ind]) {
        if (layoutClass != "none") {
          assert(layoutClass == ancestorMap[ptIt] &&
            "All parents of a primitive vtable should have the same root layout.");
        } else
          layoutClass = ancestorMap[ptIt];
      }

      // No parents - then our "layout class" is ourselves.
      if (layoutClass == "none")
        layoutClass = className;
      
      // record the class name of the sub-object
      subObjNameMap[className].push_back(layoutClass);
    }
  }
}

/*Paul:
convert module node (metadata) to Global variable*/
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

/* Paul:
this method extracts the metadata for each module.
This is used in the buildClouds method from above.
*/
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
    /*Paul: 
     get from the module all the operands and convert them a Global Variable (GV)
    */
    GlobalVariable* classVtbl = sd_mdnodeToGV(md->getOperand(op++));

    if (classVtbl) {
      info.className = classVtbl->getName();
    }
    
    /*Paul:
    get the total number of operands for each module*/
    unsigned numOperands = sd_getNumberFromMDTuple(md->getOperand(op++)->getOperand(0));

    for (unsigned i = op; i < op + numOperands; ++i) {
      SDBuildCHA::nmd_sub_t subInfo;
      llvm::MDTuple* tup = dyn_cast<llvm::MDTuple>(md->getOperand(i));
      assert(tup);
      /*Paul:
      assert that the tuple has exactly 5 operands, 
      this will be used next*/
      assert(tup->getNumOperands() == 5);

      subInfo.order = sd_getNumberFromMDTuple(tup->getOperand(0)); // Paul: this gives the order
      subInfo.start = sd_getNumberFromMDTuple(tup->getOperand(1)); //Paul: this gives the start address
      subInfo.end = sd_getNumberFromMDTuple(tup->getOperand(2));   // Paul: this gives the end address
      subInfo.addressPoint = sd_getNumberFromMDTuple(tup->getOperand(3)); //Paul: this gives the address point
      llvm::MDTuple* parentsTup = dyn_cast<llvm::MDTuple>(tup->getOperand(4)); //Paul: this returns the parents tuple

      /*Paul: get the number of parents for the first parent
      */
      unsigned numParents = sd_getNumberFromMDTuple(parentsTup->getOperand(0)); 
      for (int j = 0; j < numParents; j++) {
        vtbl_name_t ptName = sd_getStringFromMDTuple(parentsTup->getOperand(1+j*3)); //Paul: give the name of the v table
        unsigned ptIdx = sd_getNumberFromMDTuple(parentsTup->getOperand(1+j*3+1)); // Paul: one more index position to the right, get pointer index
        GlobalVariable* parentVtable = sd_mdnodeToGV(parentsTup->getOperand(1+j*3+2).get()); // Paul: one more index to the right, 
                                                                                             // get the node and convert to global variable
        if (parentVtable) {
          ptName = parentVtable->getName();
        }
        subInfo.parents.insert(vtbl_t(ptName, ptIdx));
      }

      bool currRangeCheck = (subInfo.start <= subInfo.addressPoint &&
                             subInfo.addressPoint <= subInfo.end);
      bool prevVtblCheck = (i == op || (--info.subVTables.end())->end < subInfo.start);

      assert(currRangeCheck && prevVtblCheck); // Paul: this conditions have to hold

      info.subVTables.push_back(subInfo);
    }
    op += numOperands;

    if (classes.count(info.className) == 0) {
      classes.insert(info.className);
      infoVec.push_back(info);
    }
  } while (op < md->getNumOperands()); // Paul: iterate until all module operands have been visited

  return infoVec;
}

int64_t SDBuildCHA::getCloudSize(const SDBuildCHA::vtbl_name_t& vtbl) {
  vtbl_t v(vtbl, 0);
  return cloudSizeMap[v];
}

uint32_t SDBuildCHA::calculateChildrenCounts(const SDBuildCHA::vtbl_t& root){
  uint32_t count = isDefined(root) ? 1 : 0;
  if (cloudMap.find(root) != cloudMap.end()) { // Paul: check if cloud is not empty
    for (const SDBuildCHA::vtbl_t& n : cloudMap[root]) { //Paul: the cloud map has several root nodes
      //Paul: the number of children is determined for each root node
      count += calculateChildrenCounts(n);
    }
  }

  //assert(cloudSizeMap.find(root) == cloudSizeMap.end());
  cloudSizeMap[root] = count;

  return count;
}

/*Paul:
after the CHA analysis the results will be cleared*/
void SDBuildCHA::clearAnalysisResults() {
  cloudMap.clear();
  roots.clear();
  addrPtMap.clear();
  rangeMap.clear();
  ancestorMap.clear();
  oldVTables.clear();
  cloudSizeMap.clear();

  sd_print("Cleared SDBuildCHA analysis results ... \n");
}

/// ----------------------------------------------------------------------------
/// Helper functions
/// ----------------------------------------------------------------------------

/*Paul: print the clouns in .dot files and open than afterwards with GraphViz for display*/
void SDBuildCHA::printClouds(const std::string &suffix) {
  int rc = system("rm -rf /tmp/dot && mkdir /tmp/dot");
  assert(rc == 0);

  for(const vtbl_name_t& rootName : roots) {
    assert(rootName.length() <= 490);

    char filename[512];
    sprintf(filename, "/tmp/dot/%s.%s.dot", rootName.data(), suffix.c_str());

    FILE* file = fopen(filename, "w");
    assert(file);

    fprintf(file, "digraph %s {\n", rootName.data());

    vtbl_t root(rootName,0);

    std::deque<vtbl_t> classes;
    std::set<vtbl_t> visited;
    classes.push_back(root);

    while(! classes.empty()) {
      vtbl_t vtbl = classes.front();

      fprintf(file, "\t \"(%s,%lu)\";\n", vtbl.first.data(), vtbl.second);
      classes.pop_front();

      for (const vtbl_t& child : cloudMap[vtbl]) {
        fprintf(file, "\t \"(%s,%lu)\" -> \"(%s,%lu)\";\n",
                vtbl.first.data(), vtbl.second,
                child.first.data(), child.second);
        if (visited.find(child) == visited.end()) {
          classes.push_back(child);
          visited.insert(child);
        }
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

bool SDBuildCHA::isAncestor(const vtbl_t &base, const vtbl_t &derived) {
  if (derived == base)
    return true;

  const vtbl_set_t &pts = parentMap[derived.first][derived.second];
  /*
  std::cerr << "parents of " << derived.first << "," << derived.second << ":";
  for (auto pt : pts)
    std::cerr << pt.first << "," << pt.second << ";";
  std::cerr << "\n";
  */
  for (auto pt : pts) {
    if (isAncestor(base, pt))
      return true;
  }
  return false;
}

/*Paul:
this function allready talks about upcasting. This can be used in the future
to build a tool which detects not allowed casts*/
int64_t 
SDBuildCHA::getSubVTableIndex(const vtbl_name_t& derived, const vtbl_name_t &base) {
  
  int res = -1;
  for (int64_t ind = 0; ind < subObjNameMap[derived].size(); ind++) {
    if (isAncestor(vtbl_t(base, 0), vtbl_t(derived, ind))) {
      if (res != -1) {
        std::cerr << "Ambiguity: not a unique path for upcast " << derived << " to " << base << "\n";
        return -1;
      }
      res = ind;
    }
  }
  return res;
}
