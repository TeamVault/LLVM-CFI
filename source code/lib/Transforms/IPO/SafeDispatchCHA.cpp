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
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>
#include <vector>
#include <set>
#include <map>
#include <math.h>
#include <algorithm>
#include <deque>

// you have to modify the following 4 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManagerBuilder.cpp

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

//Paul: this runs recursivelly until all nodes where visited 
// it is called once from the preorder function from underneath 
void SDBuildCHA::preorderHelper(std::vector<SDBuildCHA::vtbl_t>& nodes, 
                                   const SDBuildCHA::vtbl_t& root, 
                                              vtbl_set_t &visited) {
  //Paul: while not each node was visited 
  if (visited.find(root) != visited.end())
    return;//in case all nodes were visited than stop the recursion 

  nodes.push_back(root);// ad the node to the preorder traversal 
  visited.insert(root);//now it is visited 

  // Paul: if the node is found, this means before
  // reaching the end of the set the node has to be found 
  if (cloudMap.find(root) != cloudMap.end()) { 
    for (const SDBuildCHA::vtbl_t& n : cloudMap[root]) {
      preorderHelper(nodes, n, visited); //Paul: recursive call 
    }
  }
}

//Paul: return the nodes in preorder for the given root node 
std::vector<SDBuildCHA::vtbl_t> SDBuildCHA::preorder(const vtbl_t& root) {
  //vector of pairs (std::pair<vtbl_name_t, uint64_t> )
  order_t nodes;

  //set of pairs (std::pair<vtbl_name_t, uint64_t>)
  vtbl_set_t visited;
  preorderHelper(nodes, root, visited);
  return nodes;
}

static inline uint64_t sd_getNumberFromMDTuple(const MDOperand& op) {
  Metadata* md = op.get();
  assert(md);
  ConstantAsMetadata* cam = dyn_cast_or_null<ConstantAsMetadata>(md);
  assert(cam);
  ConstantInt* ci = dyn_cast<ConstantInt>(cam->getValue());
  assert(ci);

  return ci->getSExtValue();
}

static inline SDBuildCHA::vtbl_name_t sd_getStringFromMDTuple(const MDOperand& op) {
  MDString* mds = dyn_cast_or_null<MDString>(op.get());
  assert(mds);

  return mds->getString().str();
}

//check that the cloud map is not empty for each of the root nodes 
void SDBuildCHA::verifyClouds(Module &M) {
  //Paul: iterate throug all roots 
  for (auto rootName : roots) {
    vtbl_t root(rootName, 0);
    assert(cloudMap.count(root)); //Paul: check that the cloud map for each of the roots is not empty  
  }
}

 /**Paul: this a great place to improve. Their implementation is not optimal.
 This method is not even used at all in the initial implementation.
  */
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
    // children conservatively terminate search.

    // Paul: the search can be continued if we know the base class of the method
    // at the call site on which the object is calling. The base class for a method
    // is a class where that method was first declared. So by knowing this information
    // we filter out v tables when searching among the children of a node. We need to
    // select only the v tables which are descendants (inherited) from this base class.
    // this v tables have to be on an inheritance path 
    if (nChildrenCommonAncestors != 1)
      break;

    candidate = nextCandidate;
  } while (1); //Paul: run until breack is called 

  return candidate;
}

/*Paul: this is the main method in this class. This method builds the:
cloudMap
rangeMap
parentsMap
roots
addrPtMap
*/
void SDBuildCHA::buildClouds(Module &M) {
  // this set is used for checking if a parent class is defined or not
  std::set<vtbl_t> build_undefinedVtables;

  for(auto itr = M.getNamedMDList().begin(); itr != M.getNamedMDList().end(); itr++) {
    
    //Paul: get all metadata of this module
    NamedMDNode* md = itr;

    // only look at the modules we created and in 
    // which we added our class metadata. 
    if(! md->getName().startswith(SD_MD_CLASSINFO))
      continue;

    sd_print("\nGOT METADATA: %s\n", md->getName().data());

    // Paul: extractMetadata() extracts the metadata from each module
    // and puts it into this vector, this metadata was previously added 
    // inside SafeDispatchVtblMD.h, in: sd_insertVtableMD() function
    // this function is called for each generated v table, during code generation  
    std::vector<nmd_t> infoVec = extractMetadata(md);

    //nmd_t is the main top root node type, now iterate through the info vector   
    for (const nmd_t& info : infoVec) {
     
      // record the old vtable array
      /* Paul:
      this GlobalVariable holds the metadata for each module.
      Inside the metadata the v tables are contained.
      */
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
      
      //Paul: iterate trough the sub v tables of the metadata vector
      // and build the roots, parents, addres pointer and the range maps
      // for each root node 
      for(unsigned ind = 0; ind < info.subVTables.size(); ind++) {
        const nmd_sub_t* subInfo = & info.subVTables[ind];
        vtbl_t name(info.className, ind);
        
        sd_print("SubVtable: %d Order: %d clossest Parents count: %d ",
          ind, 
          subInfo->order,
          subInfo->parents.size());

        for (auto it : subInfo->parents) {
          sd_print("subInfo parents (%s, %d),", it.first.c_str(), it.second);
        }

        for (auto &entry : subInfo->functions) {
          sd_print("subInfo functions (%s @ %d),", entry.functionName.c_str(), entry.offsetInVTable);
        }
        vTableFunctionMap[name] = subInfo->functions;

        sd_print("subInfo start-end [%d-%d] AddrPt: %d\n",
          subInfo->start,
          subInfo->end,
          subInfo->addressPoint);
        

        if (build_undefinedVtables.find(name) != build_undefinedVtables.end()) {
          //sd_print("Removing %s,%d from build_udnefinedVtables\n", name.first.c_str(), name.second);
          build_undefinedVtables.erase(name);
        }

        if (cloudMap.find(name) == cloudMap.end()){
          //sd_print("Inserting vtable: %s, order: %d in cloudMap\n", name.first.c_str(), name.second);
          //Paul: here the cloudMap is filled for the first time 
          cloudMap[name] = std::set<vtbl_t>(); //empty set
        }

        vtbl_set_t parents;
        
        //Paul: interate now through each subinfo and get the parents
        for (auto it : subInfo->parents) {
          if (it.first != "") {
            vtbl_t &parent = it;
            parents.insert(parent); // parent is a pair of <vtbl_name_t, uint64_t> 

            // if the parent class is not defined yet, add it to the
            // undefined vtable set
            if (cloudMap.find(parent) == cloudMap.end()) {
              //sd_print("Inserting %s, %d in cloudMap - undefined parent\n", parent.first.c_str(), parent.second);
              cloudMap[parent] = std::set<vtbl_t>();
              build_undefinedVtables.insert(parent);
            }

            // add the current class to the parent's children set
            sd_print("root: %s in cloudMap insert vtable: %s, \n",  parent.first.c_str(), name.first.c_str());
            cloudMap[parent].insert(name);
          } else {
            assert(ind == 0); // make sure secondary vtables have a direct parent
            
            // add the class to the root set
            roots.insert(info.className);
          }
        }
        
        // Paul: record the parents for each class 
        parentMap[info.className].push_back(parents); //parents set 

        // record the original address points for each class 
        addrPtMap[info.className].push_back(subInfo->addressPoint);

        // record the sub-vtable ends for each class
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
  
  //Paul: assertion to check that the are no undefined v tables
  assert(build_undefinedVtables.size() == 0);
  
  //Paul: build the ancestor map for each of the child nodes of a root node
  for (auto rootName : roots) {
    vtbl_t root(rootName, 0);
    for (auto child : preorder(root)) {
      if (ancestorMap.find(child) == ancestorMap.end()) {
        ancestorMap[child] = rootName;
      }
    }
  }

  //Paul: print the parent map for each of the classes 
  for (auto it : parentMap) {
    const vtbl_name_t &className = it.first;
    const std::vector<vtbl_set_t> &parentSetV = it.second;

    std::cerr << "(class name: " << className << ", parents: [";

    for (int ind = 0; ind < parentSetV.size(); ind++) {
      std::cerr << "index: "<< ind <<"{";
      for (auto ptIt : parentSetV[ind])
        std::cerr << "<" << ptIt.first << "," << ptIt.second << ">,";
      std::cerr << "},";
    }

    std::cerr << "]\n";
  }
  
  //Paul: Check that all possible parents are in the same layout cloud
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
          layoutClass = ancestorMap[ptIt];//set the layout class 
      }

      // No parents - then our "layout class" is ourselves.
      if (layoutClass == "none")
        layoutClass = className;

      // record the class name of the sub-object
      subObjNameMap[className].push_back(layoutClass);
    }
  }
}

std::deque<SDBuildCHA::vtbl_name_t> SDBuildCHA::topoSort() {
  std::deque<vtbl_name_t> ordered;
  std::set<vtbl_name_t> visited;
  std::set<vtbl_name_t> tempMarked;
  for (auto &root : roots) {
    topoSortHelper(root, ordered, visited, tempMarked);
  }

  for (auto &entry : ordered) {
    sdLog::log() << entry << "\n";
  }
  return ordered;
}

void SDBuildCHA::topoSortHelper(vtbl_name_t node, std::deque<vtbl_name_t> &ordered,
                                std::set<vtbl_name_t> &visited, std::set<vtbl_name_t> &tempMarked) {
  if (visited.find(node) !=  visited.end())
    return;

  assert(tempMarked.find(node) == tempMarked.end() && "CHA is cyclic!?");
  tempMarked.insert(node);

  for (auto &child : cloudMap[vtbl_t(node, 0)]) {
    topoSortHelper(child.first, ordered, visited, tempMarked);
  }
  visited.insert(node);
  ordered.push_front(node);
}

void SDBuildCHA::buildFunctionInfo() {
  currentID = 1;
  std::deque<vtbl_name_t> topologicalOrder = topoSort();

  std::vector<FunctionEntry> functionImpls;
  for (auto &className : topologicalOrder) {
    int ind = 0;
    for (auto &function : vTableFunctionMap[vtbl_t(className, 0)]) {
      if (functionImplMap.find(function.functionName) == functionImplMap.end()) {
        sdLog::log() << "new impl: " << function << "\n";
        std::vector<FunctionEntry> entriesForFunction;

        int directOverride = 0;
        for (auto & parent : parentMap[className][0]) {
          if (ind < vTableFunctionMap[parent].size()) {
            sdLog::log() << "\t is direct override of" << parent.first << ", " << parent.second << "@" << ind << "\n";
            directOverride++;
          }
        }

        entriesForFunction.push_back(function);

        int indirectOverride = 0;
        for (int64_t i = 1; i < subObjNameMap[className].size(); i++) {
          for (auto &overrideFunc : vTableFunctionMap[vtbl_t(className, i)]) {
            if (function.functionName == overrideFunc.functionName) {
              sdLog::log() << "\t is indirect override: " << overrideFunc << "\n";
              entriesForFunction.push_back(overrideFunc);
              indirectOverride++;
            }
          }
        }

        int totalOverrides = directOverride + indirectOverride;
        if (totalOverrides > 1) {
          sdLog::warn() << "Function "<< function.functionName << " overrides "
                        << totalOverrides << " times!\n";
        }

        functionImpls.push_back(function);
        functionImplMap[function.functionName] = entriesForFunction;
      }

      ind++;
    }
  }

  for (auto &function : functionImpls) {
    func_and_class_t funcAndClass(function.functionName, function.vTable.first);

    if (functionMap.find(funcAndClass) == functionMap.end()) {
      sdLog::log() << "New base function: " << function << "\n";
      buildFunctionInfoForFunction(function, function.functionName);
    }
  }

  for (auto &entry : functionMap) {
    for (auto &function : entry.second) {
      auto range = functionRangeMap.find(function);
      if (range != functionRangeMap.end()) {
        sdLog::log() << function << ": ("
                     << range->second.first << "-"
                     << range->second.second << ")\n";
      } else {
        sdLog::warn() << function << " in functionMap has no range\n";
      }
    }
  }

  for (auto &entry : functionImplMap) {
    for (auto &function : entry.second) {
      auto ID = functionIDMap.find(function);
      if (ID != functionIDMap.end()) {
        sdLog::log() << function << ": " << ID->second << "\n";
      } else {
        sdLog::warn() << function << " in functionImplMap has no ID\n";
      }
    }
  }
}

SDBuildCHA::range_t SDBuildCHA::buildFunctionInfoForFunction(FunctionEntry &function, std::string rootFunctionName) {
  sdLog::log() << "Function : " << function;

  // functionMap
  func_and_class_t funcAndClass(function.functionName, function.vTable.first);
  if (functionMap.find(funcAndClass) != functionMap.end()) {
    sdLog::warn() << "\nFunction "<< function << " was encountered multiple times!\n";
  }
  functionMap[funcAndClass].push_back(function);

  // analysis
  functionParentMap[function.functionName] = rootFunctionName;

  // functionIDMap
  assert(functionIDMap.find(function) == functionIDMap.end() && "Function already has an ID?");
  sdLog::logNoToken() << " -> " << currentID << "\n";
  range_t result(currentID, currentID);
  functionIDMap[function] = currentID++;

  // recurse for children
  for (auto &child : cloudMap[function.vTable]) {
    FunctionEntry *childFunction = nullptr;
    for (auto &entry : vTableFunctionMap[child]) {
      if (entry.offsetInVTable == function.offsetInVTable) {
        childFunction = &entry;
      }
    }
    assert(childFunction && "Child vtable does not copy function from parent!");
    range_t subRange = buildFunctionInfoForFunction(*childFunction, rootFunctionName);

    assert(result.second + 1 == subRange.first && "Range is not consistent!");
    result.second = subRange.second;
  }

  sdLog::log() << "Final range: " << function << " -> (" << result.first << "-" << result.second << ")\n";

  functionRangeMap[function] = result;
  return result;
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
std::vector<SDBuildCHA::nmd_t> SDBuildCHA::extractMetadata(NamedMDNode* md) {
  
  std::set<vtbl_name_t> classes;
  std::vector<SDBuildCHA::nmd_t> infoVec;

  unsigned op = 0;

  // Paul: iterate until all module operands have been visited
  do {
    SDBuildCHA::nmd_t info;

    MDString* infoMDstr = dyn_cast_or_null<MDString>(md->getOperand(op++)->getOperand(0));
    assert(infoMDstr);
    info.className = infoMDstr->getString().str();
   
    /*Paul: 
     get from the module the operands one by one and convert them a Global Variable (GV)
    */
    GlobalVariable* classVtbl = sd_mdnodeToGV(md->getOperand(op++));

    if (classVtbl) {
      info.className = classVtbl->getName();
    }
    
    /*Paul:
    get the number of operands for the first operand 0 for each of the module operands*/
    unsigned numOperands = sd_getNumberFromMDTuple(md->getOperand(op++)->getOperand(0));

    for (unsigned i = op; i < op + numOperands; ++i) {

      SDBuildCHA::nmd_sub_t subInfo;//Paul: this will be added into the info struct

      llvm::MDTuple* tup = dyn_cast<llvm::MDTuple>(md->getOperand(i));
      assert(tup);
      
      /*Paul:
      assert that the tuple has exactly 6 operands,
      this will be used next*/
      assert(tup->getNumOperands() == 6);

      //Paul: these are the 6 operand mentioned above
      subInfo.order             = sd_getNumberFromMDTuple(tup->getOperand(0)); //Paul: this gives the order
      subInfo.start             = sd_getNumberFromMDTuple(tup->getOperand(1)); //Paul: this gives the start address
      subInfo.end               = sd_getNumberFromMDTuple(tup->getOperand(2)); //Paul: this gives the end address
      subInfo.addressPoint      = sd_getNumberFromMDTuple(tup->getOperand(3)); //Paul: this gives the address point
      llvm::MDTuple* parentsTup = dyn_cast<llvm::MDTuple>(tup->getOperand(4)); //Paul: this returns the parents tuple
      llvm::MDTuple* functionsTup = dyn_cast<llvm::MDTuple>(tup->getOperand(5)); //Paul: this returns the functions tuple

      /*Paul: get the number of parents for the first parent*/
      unsigned numParents = sd_getNumberFromMDTuple(parentsTup->getOperand(0)); 
      
      //Paul: iterate through all the parents 
      for (int j = 0; j < numParents; j++) {
        
        //Paul: regive the name of the v table
        vtbl_name_t ptName = sd_getStringFromMDTuple(parentsTup->getOperand(1+j*3)); 

        // Paul: one more index position to the right, get pointer index
        unsigned ptIdx = sd_getNumberFromMDTuple(parentsTup->getOperand(1+j*3+1)); 

        // Paul: one more index to the right, get the node and convert to global variable
        GlobalVariable* parentVtable = sd_mdnodeToGV(parentsTup->getOperand(1+j*3+2).get()); 
        
        if (parentVtable) {
          ptName = parentVtable->getName();
        }
        
        subInfo.parents.insert(vtbl_t(ptName, ptIdx));
      }

      /*Matt: get the number of functions in this sub vtable */
      unsigned numFunctions = sd_getNumberFromMDTuple(functionsTup->getOperand(0));

      //Matt: iterate through all the functions
      for (int j = 0; j < numFunctions; j++) {

        //Matt: retrieve the mangled name of the function
        std::string funcName = sd_getStringFromMDTuple(functionsTup->getOperand(1+j*2));
        uint64_t offset = sd_getNumberFromMDTuple(functionsTup->getOperand(1+j*2+1));
        subInfo.functions.push_back(FunctionEntry(funcName, vtbl_t(info.className, subInfo.order), offset));
      }

      bool currRangeCheck = (subInfo.start <= subInfo.addressPoint && subInfo.addressPoint <= subInfo.end);
      bool prevVtblCheck = (i == op || (--info.subVTables.end())->end < subInfo.start);

      assert(currRangeCheck && prevVtblCheck); // Paul: this conditions have to hold
      
      //Paul: add the subInfos to the info 
      info.subVTables.push_back(subInfo); //Paul: subInfo struct is contained in the info struct
    }
    
    //count the number of operands, in order to know when to stop 
    op += numOperands;

    if (classes.count(info.className) == 0) {
        classes.insert(info.className);
        //Paul: put the info in the infoVec vector 
        infoVec.push_back(info);
    }
  } while (op < md->getNumOperands()); // Paul: iterate until all module operands have been visited

  return infoVec;
}

//returns the number of children in that sub cloud 
int64_t SDBuildCHA::getCloudSize(const SDBuildCHA::vtbl_name_t& vtbl) {
  vtbl_t v(vtbl, 0);
  return cloudSizeMap[v];//returns the cloud size for a certain v table 
}

//calculate number of children for a single root node 
uint32_t SDBuildCHA::calculateChildrenCounts(const SDBuildCHA::vtbl_t& root){
  uint32_t count = isDefined(root) ? 1 : 0;
  if (cloudMap.find(root) != cloudMap.end()) { // Paul: check if cloud is not empty
    for (const SDBuildCHA::vtbl_t& n : cloudMap[root]) { //Paul: the cloud map has several root nodes
      //Paul: the number of children is determined for each root node
      //sd_print("list each vtable %s for a given root: %s \n", n.first.c_str(), root.first.c_str());
      count += calculateChildrenCounts(n);
    }
  }

  //sd_print("Root: %s count: %d \n", root.first.c_str(), count);
  //assert(cloudSizeMap.find(root) == cloudSizeMap.end());
  cloudSizeMap[root] = count;

  return count;
}

/* Paul:
after the CHA analysis the results will be cleared */
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

/*Paul: 
print the clouns in a .dot file and open 
than afterwards with GraphViz for display
This file is located in the tmp/dot folder.
*/
void SDBuildCHA::printClouds(const std::string &suffix) {
  int rc = system("rm -rf /tmp/dot && mkdir /tmp/dot");
  
  assert(rc == 0);

  //Paul: these roots are only the name of the classes.
  for(const vtbl_name_t& rootName : roots) {
    assert(rootName.length() <= 490);//Paul: hard bound set on the length of each root name 

    char filename[512];
    sprintf(filename, "/tmp/dot/%s.%s.dot", rootName.data(), suffix.c_str());

    FILE* file = fopen(filename, "w");
    assert(file);

    fprintf(file, "digraph %s {\n", rootName.data());

    vtbl_t root(rootName,0);
    
    //Paul: all classes names
    std::deque<vtbl_t> classes;

    //Paul: all visited 
    std::set<vtbl_t> visited;

    classes.push_back(root);

    //until classe are not empty, all classes visited once 
    while(! classes.empty()) {

      //extract front element 
      vtbl_t vtbl = classes.front();

      fprintf(file, "\t \"(%s,%lu)\";\n", vtbl.first.data(), vtbl.second);

      //remove fron element 
      classes.pop_front();
      
      //iterate through all children of this root 
      for (const vtbl_t& child : cloudMap[vtbl]) {
        fprintf(file, "\t \"(%s,%lu)\" -> \"(%s,%lu)\";\n",
                          vtbl.first.data(), vtbl.second,
                          child.first.data(), child.second);
        if (visited.find(child) == visited.end()) {
          //add class name
          classes.push_back(child);

          //add visited child 
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
int64_t SDBuildCHA::getSubVTableIndex(const vtbl_name_t& derived, const vtbl_name_t &base) {
  
  int res = -1;
  for (int64_t ind = 0; ind < subObjNameMap[derived].size(); ind++) {

    //check if base is an acestor of one of the derived classes 
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
