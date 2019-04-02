#include "llvm/Support/raw_ostream.h"
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
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Transforms/IPO/SafeDispatchLayoutBuilder.h"
#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <llvm/Transforms/IPO/SafeDispatchLogStream.h>

// you have to modify the following 4 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManagerBuilder.cpp

using namespace llvm;

#define WORD_WIDTH 8
#define NEW_VTABLE_NAME(vtbl) ("_SD" + vtbl)
#define NEW_VTHUNK_NAME(fun,parent) ("_SVT" + parent + fun->getName().str())
#define GEP_OPCODE      29

char SDLayoutBuilder::ID = 0;

INITIALIZE_PASS_BEGIN(SDLayoutBuilder, "sdovt", "Oredered VTable Layout Builder for SafeDispatch", false, false)
INITIALIZE_PASS_DEPENDENCY(SDBuildCHA) //Paul: depends on this pass
INITIALIZE_PASS_END(SDLayoutBuilder, "sdovt", "Oredered VTable Layout Builder for SafeDispatch", false, false)

static bool sd_isVthunk(const llvm::StringRef& name) {
  return name.startswith("_ZTv") || // virtual thunk
         name.startswith("_ZTcv");  // virtual covariant thunk
}

/**Paul:
this function is used to dump the new layout. 
It is used 7 times in this pass in order to check if
the new layout are ok, as expected)
The check is done by printing the v table in the terminal*/
static void dumpNewLayout(const SDLayoutBuilder::interleaving_list_t &interleaving) {
  uint64_t ind = 0;
  std::cerr << "New vtable layout:\n";
  for (auto elem : interleaving) {
    std::cerr << ind << " : " << elem.first.first << "," << elem.first.second << "[" << elem.second << "]\n";
    ind ++;
  }
}

/**Paul:
check that the v table layouts are ok (match with the old ones)
This function is called from runonModule() located in SDLayoutBuilder.h
*/
bool SDLayoutBuilder::verifyNewLayouts(Module &M) {
  
  new_layout_inds_map_t indMap;

  //iterate through all the roots 
  for (auto vtblIt = cha->roots_begin(); vtblIt != cha->roots_end(); vtblIt++) { 
    vtbl_name_t vtbl = *vtblIt;
    vtbl_t root(vtbl, 0);

    //make a copy of the interleaving map obtained during interleaving or ordering
    interleaving_list_t &interleaving = interleavingMap[vtbl];
    uint64_t i = 0;

    indMap.clear();
  
    std::cerr << "Verifying cloud : " << vtbl << "\n";
    // Build a map (vtbl_t -> (uint64_t -> uint64_t)) with the old-to-new index mapping encoded in the
    // interleaving
    for (auto elem : interleaving) {
      vtbl_t &vname = elem.first;        //string
      uint64_t oldPos = elem.second; //uint64_t
      
      //skyp dummy v tables 
      //dummyVtable = vtbl_t("DUMMY_VTBL", 0);
      if (vname == dummyVtable) 
        continue;

      if (indMap.find(vname) == indMap.end()) {
        indMap[vname] = std::map<uint64_t, uint64_t>();
      } else {
        if (indMap[vname].count(oldPos) != 0) {
          std::cerr << "In ivtbl " << vtbl << " entry " << vname.first << "," << vname.second << "[" << oldPos << "]"
            << " appears twice - at " << indMap[vname][oldPos] << " and " << i << std::endl;
         
          //Paul: dump layout in case of mismatch
          dumpNewLayout(interleaving);
          return false;
        }
      }

      indMap[vname][oldPos] = i;
      i++;
    }


    SDBuildCHA::order_t cloud = cha->preorder(root);
    std::map<vtbl_t, uint64_t> orderMap;

    for (uint64_t i = 0; i < cloud.size(); i++)
      orderMap[cloud[i]] = i;

    // 1) Check that we have a (DENSE!) map of indices for each vtable in the cloud in the
    // current interleaving. (i.e. inside indMap)
    for (const vtbl_t& n : cloud) {
      // Skip undefined vtables
      if (cha->isUndefined(n.first)) {
        // TODO: Assert that it does not appear in the interleaved vtable.
        continue;
      }

      if (indMap.find(n) == indMap.end()) {
          std::cerr << "In ivtbl " << vtbl << " missing " << n.first << "," << n.second << std::endl;
          
          //Paul: dump layout in case of mismatch
          dumpNewLayout(interleaving);
          return false;
      }

      // Check that the index map is dense (total on the range of indices)
      const range_t &r = cha->getRange(n);
      uint64_t oldVtblSize = r.second - (r.first - prePadMap[n]) + 1;
      auto minMax = std::minmax_element (indMap[n].begin(), indMap[n].end());

      if ((minMax.second->first - minMax.first->first + 1) != oldVtblSize) {
          std::cerr << "In ivtbl " << vtbl << " min-max rangefor "
            << n.first << "," << n.second << 
            " is (" << minMax.first->first << "-"
            << minMax.second->first << ") expected size "
            << oldVtblSize << std::endl;
          
          //Paul: dump layout in case of mismatch
          dumpNewLayout(interleaving);
          return false;
      }

      if (indMap[n].size() != oldVtblSize) {
          std::cerr << "In ivtbl " << vtbl << " index mapping for " << n.first << "," << n.second << 
            " has " << indMap[n].size() << " expected " << oldVtblSize << std::endl;
          
          //Paul: dump layout in case of mismatch
          dumpNewLayout(interleaving);
          return false;
      }
    }

    //Paul: no need to check if interleaving was not performed
    if (!interleave) 
      return true;

    // 1.5) Check that for each parent/child
    // the child is contained in the parent
    for (const vtbl_t& parent : cloud) {
      if (cha->isUndefined(parent.first))  continue;

      for(auto child = cha->children_begin(parent); child != cha->children_end(parent); child++) {
        if (cha->isUndefined(child->first))  continue;

        if (orderMap[*child] < orderMap[parent]) continue;

        const range_t &ptR = cha->getRange(parent);
        const range_t &chR = cha->getRange(*child);

        uint64_t ptStart     = ptR.first;
        uint64_t ptEnd       = ptR.second;
        uint64_t ptAddrPt    = cha->addrPt(parent);
        uint64_t ptRelAddrPt = ptAddrPt - ptStart;

        uint64_t childStart     = chR.first;
        uint64_t childEnd       = chR.second;
        uint64_t childAddrPt    = cha->addrPt(*child);
        uint64_t childRelAddrPt = childAddrPt - childStart;

        if ((ptAddrPt - ptStart + prePadMap[parent]) > (childAddrPt - childStart + prePadMap[*child]) ||
            ptEnd - ptAddrPt > childEnd - childAddrPt) {
              
          sd_print("Parent vtable(%s,%d) [%d-%d,%d,%d] is not contained in child vtable(%s,%d) [%d-%d,%d,%d]",
              parent.first.c_str(), parent.second, ptStart, prePadMap[parent],ptAddrPt, ptEnd, 
              child->first.c_str(), child->second, childStart, prePadMap[*child], childAddrPt, childEnd);
         
          //Paul: dump the new layout in case the parent layout is not contained in the child.   
          dumpNewLayout(interleaving);
          return false;
        }
      }
    }


    // 2) Check that the relative vtable offsets are the same for every parent/child class pair
    for (const vtbl_t& pt : cloud) {
      if (cha->isUndefined(pt.first))  continue;

      for(auto child = cha->children_begin(pt); child != cha->children_end(pt); child++) {
        if (cha->isUndefined(child->first))  continue;

        if (orderMap[*child] < orderMap[pt]) continue;
        const range_t &ptR = cha->getRange(pt);
        const range_t &chR = cha->getRange(*child);


        uint64_t ptStart = ptR.first;
        uint64_t ptEnd = ptR.second;
        uint64_t ptAddrPt = cha->addrPt(pt);
        uint64_t ptRelAddrPt = ptAddrPt - ptStart;
        uint64_t childStart = chR.first;
        uint64_t childEnd = chR.second;
        uint64_t childAddrPt = cha->addrPt(*child);
        uint64_t childRelAddrPt = childAddrPt - childStart;
        int64_t ptToChildAdj = childAddrPt - ptAddrPt;
        uint64_t newPtAddrPt = indMap[pt][ptAddrPt];
        uint64_t newChildAddrPt = indMap[*child][childAddrPt];

        for (int64_t ind = 0; ind < ptEnd - ptStart + prePadMap[pt] + 1; ind++) {
          int64_t newPtInd =  indMap[pt][ptStart + ind - prePadMap[pt]] - newPtAddrPt;
          int64_t newChildInd = indMap[*child][ptStart + ind - prePadMap[pt] + ptToChildAdj] - newChildAddrPt;

          if (newPtInd != newChildInd) {
            sd_print("Parent (%s,%d) old relative index %d (new relative %d) mismatches child (%s,%d) corresponding old index %d (new relative %d)",
                pt.first.c_str(), pt.second, (ind - ptAddrPt), newPtInd,
                child->first.c_str(), child->second, ind + ptToChildAdj - childAddrPt, newChildInd);
           
            //Paul: dump layout when ther is a parent/child mismatch
            dumpNewLayout(interleaving);
            return false;
          }
        }
      }
    }
  }

  return true;
}

ModulePass* llvm::createSDLayoutBuilderPass(bool interleave) {
  return new SDLayoutBuilder(interleave);
}

/// ----------------------------------------------------------------------------
/// Analysis implementation
/// ----------------------------------------------------------------------------
Function* SDLayoutBuilder::getVthunkFunction(Constant* vtblElement) {
  ConstantExpr* bcExpr = NULL;

  // if this a constant bitcast expression, this might be a vthunk
  // cast and assign the vtbl element as a constant expresion 
  // check if it is a bit cast expresion, BITCAST_OPCODE == 28 
  if ((bcExpr = dyn_cast<ConstantExpr>(vtblElement)) && bcExpr->getOpcode() == BITCAST_OPCODE) {
   
   //get first operant 
    Constant* operand = bcExpr->getOperand(0);

    // vthunk contains _ZTcv or _ZTc in the name of the first operand  
    if (sd_isVthunk(operand->getName())) {

      //dynamically cast operand to a Function 
      Function* thunkF = dyn_cast<Function>(operand);
      assert(thunkF);

      //return the new function 
      return thunkF;
    }
  }

  return NULL;
}

//Paul: replace the old v call index with a new one using Intrinsic::sd_get_vcall_index
//this is necessary since the layout of the v tables is changed 
//This are the placeholders which will be filled with values of the ranges and widths 
void SDLayoutBuilder::createThunkFunctions(Module& M, const vtbl_name_t& rootName) {
  // for all defined vtables
  vtbl_t root(rootName,0);

  //Paul: preorder traversal of the whole cloud tree 
  order_t vtbls_preorder = cha->preorder(root);

  LLVMContext& C = M.getContext();

  //Paul: get the v call index function using the Intrinsic::sd_get_vcall_index 
  Function *sd_vcall_indexF = M.getFunction(Intrinsic::getName(Intrinsic::sd_get_vcall_index));
 
  //iterate through all vtables in preorder 
  //skip the v tables which are not known by the old CHA 
  for (unsigned i=0; i < vtbls_preorder.size(); i++) {
    const vtbl_name_t& vtbl = vtbls_preorder[i].first;
    
    if (!cha->hasOldVTable(vtbl)) {
      assert(cha->isUndefined(vtbl));
      continue; //skip
    }
    
    //get the old v table as an constant array 
    ConstantArray* vtableArr = cha->getOldVTable(vtbl);

    // iterate over the vtable operands 
    for (unsigned vtblInd = 0; vtblInd < vtableArr->getNumOperands(); ++vtblInd) {
      Constant* c = vtableArr->getOperand(vtblInd);

      //get v thunk function for each constant c
      Function* thunkF = getVthunkFunction(c);

      //skyp if thunkF is null 
      if (! thunkF)
        continue;

      // find the index of the sub-vtable inside the whole
      unsigned order = cha->getVTableOrder(vtbl, vtblInd);

      // this should have a parent
      const std::string& parentClass = cha->getLayoutClassName(vtbl, order);

      //create a new thunk name based on thunkF and parent Class 
      //NEW_VTHUNK_NAME(fun,parent) ("_SVT" + parent + fun->getName().str())
      //attack to the name of the thunk function the name of the parent class 
      std::string newThunkName(NEW_VTHUNK_NAME(thunkF, parentClass));
      
      //if allready exists than skip 
      if (M.getFunction(newThunkName)) {
        // we already created such function, will use that later
        continue;
      }

      // duplicate the function and rename it
      ValueToValueMapTy VMap;

      //duplicate the old thunk function 
      Function *newThunkF = llvm::CloneFunction(thunkF, VMap, false);

      //set the previously computed name 
      newThunkF->setName(newThunkName);

      //insert the new thunk function into the module function list 
      M.getFunctionList().push_back(newThunkF);

      //start replacing the old index in the instruction witht the new index 
      CallInst* CI = NULL; //declare a new call instruction 
      
      //if the function is null than skip loop iteration 
      if(sd_vcall_indexF == NULL)
        continue;

      // go over its instructions and replace the one with the metadata
      // go over each function 
      for(Function:: iterator bb_itr = newThunkF->begin(); bb_itr != newThunkF->end(); bb_itr++) {
        
        //go over each bb inside the function
        for(BasicBlock:: iterator i_itr = bb_itr->begin(); i_itr != bb_itr->end(); i_itr++) {
          Instruction* inst = i_itr;
          
          //cast to CallInstrunction and assign the instruction to CI 
          //check that the called function by thic call instrunction equals  sd_vcall_indexF
          //Function *sd_vcall_indexF = M.getFunction(Intrinsic::getName(Intrinsic::sd_get_vcall_index));
          //basically we filter for the instrunction which is calling our function which gets the v call index 
          if ((CI = dyn_cast<CallInst>(inst)) && CI->getCalledFunction() == sd_vcall_indexF) {
            
            // get the first argument, this is the v pointer 
            llvm::ConstantInt* oldVal = dyn_cast<ConstantInt>(CI->getArgOperand(0));

            //assert there is one 
            assert(oldVal);

            // extract the old index
            int64_t oldIndex = oldVal->getSExtValue() / WORD_WIDTH;

            // calculate the new index
            sd_print("NEW_VTHUNK_NAME(fun,parent) (_SVT + parent + fun->getName().str()) \n");
            sd_print("Create thunk function %s\n", newThunkName.c_str());

            //compute new index based on the fact that it is relative or not
            //in our case relative is always on  
            int64_t newIndex = translateVtblInd(vtbl_t(vtbl,order), oldIndex, true);
            
            //multiply with word_width == 8
            Value* newValue = ConstantInt::get(IntegerType::getInt64Ty(C), newIndex * WORD_WIDTH);
            
            //set the new index value in the call instrunction 
            //set the new value of the v pointer 
            CI->replaceAllUsesWith(newValue);

          }
        }
      }

      // this function should have a metadata
    }
  }
}

/*Paul: 
this function is used to order the cloud.
The ordering can be shut down and it is not dependent of
the interleaving operation. It orders each v table
one by one.
*/
void SDLayoutBuilder::orderCloud(SDLayoutBuilder::vtbl_name_t& vtbl) {
  sd_print("Started ordering for vtable: %s ...\n", vtbl.c_str());

  /*Paul:
  cha stores all the results of the CHA pass*/
  assert(cha->isRoot(vtbl));

  // create a temporary list for the positive part
  interleaving_vec_t orderedVtbl;

  vtbl_t root(vtbl,0);
  order_t pre = cha->preorder(root);
  uint64_t max = 0;

  for(const vtbl_t child : pre) {
    const range_t& r = cha->getRange(child);
    uint64_t size = r.second - r.first + 1;
    if (size > max)
      max = size;
  }

  max--;
  max |= max >> 1;   // Divide by 2^k for consecutive doublings of k up to 32,
  max |= max >> 2;   // and then or the results.
  max |= max >> 4;
  max |= max >> 8;
  max |= max >> 16;
  max |= max >> 32;
  max++;            // The result is a number of 1 bits equal to the number
                    // of bits in the original number, plus 1. That's the
                    // next highest power of 2.

  assert((max & (max-1)) == 0 && "max is not a power of 2");

  alignmentMap[vtbl] = max * WORD_WIDTH;

  //sd_print("ALIGNMENT: %s, %u\n", vtbl.data(), max*WORD_WIDTH);

  for(const vtbl_t child : pre) {
    if(cha->isUndefined(child.first))
      continue;

    const range_t &r = cha->getRange(child);
    uint64_t size = r.second - r.first + 1;
    uint64_t addrpt = cha->addrPt(child) - r.first;
    uint64_t padEntries = orderedVtbl.size() + addrpt;
    uint64_t padSize = (padEntries % max == 0) ? 0 : max - (padEntries % max);

    for(unsigned i=0; i<padSize; i++) {
      if (orderedVtbl.size() % max == 0 && orderedVtbl.size() != 0) {
        std::cerr << "dummy entry is " << max << " aligned in cloud " << vtbl << std::endl;
      }
      orderedVtbl.push_back(interleaving_t(dummyVtable,0));
    }

    for(unsigned i=0; i<size; i++) {
      orderedVtbl.push_back(interleaving_t(child, r.first + i));
    }
  }

  // store the new ordered vtable
  interleavingMap[vtbl] = interleaving_list_t(orderedVtbl.begin(), orderedVtbl.end());
  
  sd_print("Finishing ordering for vtable: %s ...\n", vtbl.c_str());
}

//check if v table lies in class or v table path inheritance
bool checkVTablePath(SDLayoutBuilder::vtbl_name_t& vtbl){
  //TODO, for now return true 
  
  return true;
}

// this is our new interleaving method
// we need to check inside the interleaving
// method for each vtbl if it lies in the class
// or v table inheritance path
void SDLayoutBuilder::interleaveCloudNew(SDLayoutBuilder::vtbl_name_t& vtbl) {
  
  // skyp v tables that do not belong
  // to the class or vtbl path of inheritance
  if(!checkVTablePath(vtbl))
  return;

  sd_print("Started New Interleaving for v table %s...\n", vtbl.c_str());
  
  /*Paul:
  check that the v table is a root in a subcloud of the main cloud. 
  The CHA pass has previously generated a cloud. 
  Each subcloud of the cloud has a root.
  */
  assert(cha->isRoot(vtbl));

  // create a temporary list for the positive part
  interleaving_list_t positive_list_Part;

  //Paul: this is a a vector of all the nodes in the sub-tree having as root the vtbl 
  vtbl_t root(vtbl,0);
  
  //Paul: return the nodes of the sub tree having 
  // as root vtbl in preorder 
  order_t preorderNodeSet = cha->preorder(root);  
  sd_print("Root node: %s has %d nodes in preoder \n", vtbl.c_str(), preorderNodeSet.size());
  
  std::map<vtbl_t, uint64_t> indMap;
  for (uint64_t i = 0; i < preorderNodeSet.size(); i++)
    indMap[preorderNodeSet[i]] = i;

  // First check if any vtable needs pre-padding. (All vtables must contain their parents).
  int numParent =0;

  //Paul: iterate through all the nodes in this sub tree 
  for (auto parent : preorderNodeSet) {
    if (cha->isUndefined(parent))
      continue; 
      
    numParent++;//Paul: count number of parents 

    int numChildrenPerParent = 0;

    //Paul: search only in the children of the current node 
    // the definition of the children should take into account
    // both the inheritance between classes and between v tables 
    for (auto child = cha->children_begin(parent); child != cha->children_end(parent); child++) {
        if (cha->isUndefined(parent))
          continue; 
        
        numChildrenPerParent++;

        if (indMap[*child] < indMap[parent])
          continue; // Earlier in the preorder traversal - visited from a different node.
        
        //Paul: get the ranges of the parent and child
        const range_t &parentRange = cha->getRange(parent);
        const range_t &childRange = cha->getRange(*child);

        //Paul: get the ranges of the parent 
        uint64_t parentStart  = parentRange.first;
        uint64_t parentEnd    = parentRange.second;
        uint64_t parentAddrPt = cha->addrPt(parent);
        
        //Paul: get the ranges of the child 
        uint64_t childStart  = childRange.first;
        uint64_t childEnd    = childRange.second;
        uint64_t childAddrPt = cha->addrPt(*child);

        uint64_t parentPreAddrPt = parentAddrPt - parentStart + prePadMap[parent];
        uint64_t childPreAddrPt  = childAddrPt  - childStart  + prePadMap[*child];

        //Paul: the prepad value for the child is eath the 
        //difference between parent (prepad address point) and of the child (prepad address point) 
        // or the old value contained in the child 
        prePadMap[*child] = (parentPreAddrPt > childPreAddrPt ?
                             parentPreAddrPt - childPreAddrPt : prePadMap[*child]);
    }
    sd_print("Parent %d name: %s has %d children ...\n", numParent, parent.first.c_str(), numChildrenPerParent);
  }

  sd_print("Total number of parents %d...\n", numParent);

  // initialize the cloud's interleaving list
  interleavingMap[vtbl] = interleaving_list_t();

  // fill the negative part of the interleaving map 
  fillVtablePart(interleavingMap[vtbl], preorderNodeSet, false); //Paul: one time with false, negative part
  
  // fill the positive part of the interleaving map 
  fillVtablePart(positive_list_Part, preorderNodeSet, true);     //Paul: one time with true , positive part

  // append the positive part to the negative part in the interleaving map 
  interleavingMap[vtbl].insert(interleavingMap[vtbl].end(), positive_list_Part.begin(), positive_list_Part.end());
  alignmentMap[vtbl] = WORD_WIDTH;
  
  sd_print("Finishing Interleaving for v table %s...\n", vtbl.c_str());
}


/*Paul: 
this function is used to interleave the cloud.
The interleaving can be shut down and it is not dependent of
the ordering operation from above
*/
void SDLayoutBuilder::interleaveCloud(SDLayoutBuilder::vtbl_name_t& vtbl) {
  sd_print("Started Interleaving for v table %s...\n", vtbl.c_str());
  
  /*Paul:
  check that the v table is a root in a subcloud of the main cloud. 
  The CHA pass has previously generated a cloud. 
  Each subcloud of the cloud has a root.
  */
  assert(cha->isRoot(vtbl)); 

  // create a temporary list for the positive part
  interleaving_list_t positive_list_Part;

  //Paul: this is a a vector of all the nodes in the sub-tree having as root the vtbl 
  vtbl_t root(vtbl,0);
  
  //Paul: return the nodes of the sub tree having 
  // as root vtbl in preorder 
  order_t preorderNodeSet = cha->preorder(root);  
  sd_print("Root node: %s has %d nodes in preoder \n", vtbl.c_str(), preorderNodeSet.size());
  
  std::map<vtbl_t, uint64_t> indMap;
  for (uint64_t i = 0; i < preorderNodeSet.size(); i++)
    indMap[preorderNodeSet[i]] = i;

  // First check if any vtable needs pre-padding. (All vtables must contain their parents).
  int numParent =0;

  //Paul: iterate through all the nodes in this sub tree 
  for (auto parent : preorderNodeSet) {
    if (cha->isUndefined(parent))
      continue; 
      
    numParent++;//Paul: count number of parents 

    int numChildrenPerParent = 0;

    //Paul: search only in the children of the current node 
    // the definition of the children should take into account
    // both the inheritance between classes and between v tables 
    for (auto child = cha->children_begin(parent); child != cha->children_end(parent); child++) {
        if (cha->isUndefined(parent))
          continue; 
        
        numChildrenPerParent++;

        if (indMap[*child] < indMap[parent])
          continue; // Earlier in the preorder traversal - visited from a different node.
        
        //Paul: get the ranges of the parent and child
        const range_t &parentRange = cha->getRange(parent);
        const range_t &childRange = cha->getRange(*child);

        //Paul: get the ranges of the parent 
        uint64_t parentStart  = parentRange.first;
        uint64_t parentEnd    = parentRange.second;
        uint64_t parentAddrPt = cha->addrPt(parent);
        
        //Paul: get the ranges of the child 
        uint64_t childStart  = childRange.first;
        uint64_t childEnd    = childRange.second;
        uint64_t childAddrPt = cha->addrPt(*child);

        uint64_t parentPreAddrPt = parentAddrPt - parentStart + prePadMap[parent];
        uint64_t childPreAddrPt  = childAddrPt  - childStart  + prePadMap[*child];

        //Paul: the prepad value for the child is eath the 
        //difference between parent (prepad address point) and of the child (prepad address point) 
        // or the old value contained in the child 
        prePadMap[*child] = (parentPreAddrPt > childPreAddrPt ?
                             parentPreAddrPt - childPreAddrPt : prePadMap[*child]);
    }
    sd_print("Parent %d has %d children ...\n", numParent, numChildrenPerParent);
  }

  sd_print("Total number of parents %d...\n", numParent);

  // initialize the cloud's interleaving list
  interleavingMap[vtbl] = interleaving_list_t();

  // fill the negative part of the interleaving map 
  fillVtablePart(interleavingMap[vtbl], preorderNodeSet, false); //Paul: one time with false, negative part
  
  // fill the positive part of the interleaving map 
  fillVtablePart(positive_list_Part, preorderNodeSet, true);     //Paul: one time with true , positive part

  // append the positive part to the negative part in the interleaving map 
  interleavingMap[vtbl].insert(interleavingMap[vtbl].end(), positive_list_Part.begin(), positive_list_Part.end());
  alignmentMap[vtbl] = WORD_WIDTH;
  
  sd_print("Finishing Interleaving for v table %s...\n", vtbl.c_str());
}

/*Paul:
calculate the new layout indices. The new indices are just counting 
how many v tables are contained in the interleavingMap per each v table 
*/
void SDLayoutBuilder::calculateNewLayoutInds(SDLayoutBuilder::vtbl_name_t& vtbl){
  
  //assert map is not empty 
  assert(interleavingMap.count(vtbl));
  sd_print("v table: %s has in the interleaving map: %d v tables \n", 
  vtbl.c_str(), interleavingMap.count(vtbl));

  uint64_t currentIndex = 0;
 
  //Paul: the interleaving map was computed in the ordering or interleaving algoritm 
  for (const interleaving_t& ivtbl : interleavingMap[vtbl]) {
    
    sd_print("NewLayoutInds for vtable (%s, %d)\n", ivtbl.first.first.c_str(), ivtbl.first.second);
    if(ivtbl.first != dummyVtable) {//Paul: do not count dummy v tables
      // record the new index of the vtable element coming from the current vtable
      newLayoutInds[ivtbl.first].push_back(currentIndex++);
    } else {
      currentIndex++;
    }
  }
}

/*Paul:
this is a helper function for the v pointer range calculator 
Here the v pointer ranges get coalesced 
*/
void SDLayoutBuilder::calculateVPtrRangesHelper(const SDLayoutBuilder::vtbl_t& vtbl, std::map<vtbl_t, uint64_t> &indMap){
  // Already computed
  if (rangeMap.find(vtbl) != rangeMap.end())
    return;
  
  //iterate trough all children of this v table and do recursive call 
  for (auto childIt = cha->children_begin(vtbl); childIt != cha->children_end(vtbl); childIt++) {
    const vtbl_t &child = *childIt;
    calculateVPtrRangesHelper(child, indMap);
  }
  
  //declare a range vector 
  std::vector<range_t> ranges;

  ranges.push_back(range_t(indMap[vtbl], indMap[vtbl]+1));
  
  //iterate trough all children of this v table and append each range at the end in ranges 
  for (auto childIt = cha->children_begin(vtbl); childIt != cha->children_end(vtbl); childIt++) {
    const vtbl_t &child = *childIt;
    ranges.insert(ranges.end(), rangeMap[child].begin(), rangeMap[child].end());
  }

  //sort the ranges 
  std::sort(ranges.begin(), ranges.end());

  // Coalesce (combine, put together) ranges, e.g., (0,1); (1,2) -> (0,2)
  //declare a vector for the coalesced ranges 
  std::vector<range_t> coalesced_ranges;

  //declare start and end variables 
  int64_t start = -1, end;
  for (auto it : ranges) {
    if (start == -1) {
      start = it.first;
      end = it.second;
    } else {
      if (it.first <= end) {
        if (it.second > end)
          end = it.second;
      } else {
        coalesced_ranges.push_back(range_t(start, end));
        start = it.first;
        end = it.second;
      }
    }
  }

  if (start != -1)
    coalesced_ranges.push_back(range_t(start,end));
  
  //print the ranges 
  sdLog::log() << "Range for: {" << vtbl.first << "," << vtbl.second << "} From ranges [";
  for (auto it : ranges)
    sdLog::log() << "(" << it.first << "," << it.second << "),";

  sdLog::log() << "] coalesced [";
  for (auto it : coalesced_ranges)
    sdLog::log() << "(" << it.first << "," << it.second << "),";

  sdLog::log() << "]\n";
  
  rangeMap[vtbl] = coalesced_ranges;
}

/*Paul:
final step of the Layout builder analysis is to check that ranges are disjoint*/
void SDLayoutBuilder::verifyVPtrRanges(SDLayoutBuilder::vtbl_name_t& vtbl){
  SDLayoutBuilder::vtbl_t root(vtbl, 0);
  
  //get the nodes in preordering for this top root node 
  order_t pre = cha->preorder(root);
  std::map<vtbl_t, uint64_t> indMap;
  std::map<vtbl_t, order_t> descendantsMap;

  for (uint64_t i = 0; i < pre.size(); i++) {
    //set the indexes 
    indMap[pre[i]] = i;

    //set the descendents map, this are the descendant nodes in prorder 
    descendantsMap[pre[i]] = cha->preorder(pre[i]); //each element is an order_t == std::vector<vtbl_t>
  }
  
  //iterate through the descendens map and check that ranges are dijoint 
  for (auto descendantVector : descendantsMap) {
    uint64_t totalRange = 0;
    int64_t lastEnd = -1;

    // Check that ranges are disjoint, they do not overlap at all
    for (auto range : rangeMap[descendantVector.first]) {
      //sum ranges up
      totalRange += range.second - range.first;

      //check that the ranges do not overlap 
      assert(lastEnd < ((int64_t)range.first));
      lastEnd = range.second;
    }

    // Sum of ranges length equals the total number of descendants
    assert(totalRange == descendantVector.second.size());

    // check that each descendent is in one of the ranges.
    for (auto descendantElement : descendantVector.second) {
      uint64_t index = indMap[descendantElement];
      bool found = false;
      for (auto range : rangeMap[descendantVector.first]) {
        //check that index is between range.first and range.second  
        if (range.first <= index && index < range.second) {
          found = true;
          //stop searching if found
          break;
        }
      }
      assert(found);
    }
  }

}

bool SDLayoutBuilder::hasMemRange(const vtbl_t &vtbl) {
  return memRangeMap.find(vtbl) != memRangeMap.end();
}

/*Paul
get the memory range*/
const std::vector<SDLayoutBuilder::mem_range_t>& SDLayoutBuilder::getMemRange(const vtbl_t &vtbl) {
  return memRangeMap[vtbl];
}

/*Paul:
calculate the v pointer ranges which will be used to constrain each
v call site*/
void SDLayoutBuilder::calculateVPtrRanges(Module& M, SDLayoutBuilder::vtbl_name_t& vtbl){
  SDLayoutBuilder::vtbl_t root(vtbl, 0); // Paul: declare a v table with name vtbl and index 0

  //Paul: nodes in preorder for one each root node one by one
  order_t preorderV = cha->preorder(root); 

  //print preorder nodes of one root node 
  sd_print("\ncalculateVPtrRanges: Preorder nodes of root %s are: \n", vtbl.c_str());
  for (uint64_t i= 0; i < preorderV.size(); i++)
    sdLog::log() << "first: " << preorderV[i].first << ", second: " << preorderV[i].second << "\n";

  std::map<vtbl_t, uint64_t> indMap;

  //set the indices in the indices map
  for (uint64_t i = 0; i < preorderV.size(); i++) 
      indMap[preorderV[i]] = i;
  
  //coalesce ranges, mix them together 
  calculateVPtrRangesHelper(root, indMap);
 
  //Paul: iterate through all the nodes for this root 
  //and print the ranges 
  sdLog::log() << "\n Printing and buid memRangeMap for root node " << vtbl.c_str() << " \n";
  for (uint64_t i = 0; i < preorderV.size(); i++) {
    sdLog::log() << "For pre node first: " << preorderV[i].first << ", and pre node second:" << preorderV[i].second << " ";

    for (auto it : rangeMap[preorderV[i]]) {
      uint64_t start = it.first,
      end = it.second,
      def_count = 0;

      //Paul: count number of times this is defined in the CHA
      //Notice, this number of times this has to be added in 
      //the memRangeMap at the end 
      for (int j = start; j < end; j++)
        //if defined then count else skip
        if (cha->isDefined(preorderV[j])) 
            def_count++;

      sdLog::log() << "(range " << start << "-" << end << " contains "
        << def_count << " defined,";
      
      //skip if not defined 
      if (def_count == 0)
        continue;
      
      //if undefined than skip it 
      while (cha->isUndefined(preorderV[start]) && start < end) {
        sdLog::log() << "skipping " << preorderV[start].first << "," << preorderV[start].second
          << ",";
        start++;
      }

      sdLog::log() << "final range " << preorderV[start].first << "," << preorderV[start].second
        << "+" << def_count << ")";
    
      // Paul: for each node a memory range will be added to the map and 
      // and a definition count will be icremented and added. Add to the memRangeMap. 
      memRangeMap[preorderV[i]].push_back(mem_range_t(newVtblAddressConst(M, preorderV[start]), def_count));
    }
    sdLog::log() << "\n";
  }
}

/*Paul
this method creates a new v table which will be added to a new constant.
The new constant will replace the old one in the end of this method.
*/
void SDLayoutBuilder::createNewVTable(Module& M, SDLayoutBuilder::vtbl_name_t& vtbl){
  
  // get the new v table from the interleaving map (interleaving or ordering)
  interleaving_list_t& newVtbl = interleavingMap[vtbl];

  // get the size
  uint64_t newSize = newVtbl.size();
  
  //set the v table to pointer type
  PointerType* vtblElemType = PointerType::get(IntegerType::get(M.getContext(), WORD_WIDTH), 0);
  
  //create and array of pointers of newSize 
  ArrayType* newArrType = ArrayType::get(vtblElemType, newSize);

  LLVMContext& Context = M.getContext();

  // fill the interleaved vtable element list
  std::vector<Constant*> newVtableElems;

  //iterate throught the interleaving list for the given v table 
  for (const interleaving_t& ivtbl : newVtbl) {
    const range_t &vrange = cha->getRange(ivtbl.first);

    //if v table is undefined or is a dummy table or vtable second < vrange first 
    if (cha->isUndefined(ivtbl.first.first) || ivtbl.first == dummyVtable || ivtbl.second < vrange.first) {

      //add a new null value into new V table elements 
      newVtableElems.push_back(Constant::getNullValue(IntegerType::getInt8PtrTy(Context)));

      //else 
    } else {

      //there is an old v table 
      assert(cha->hasOldVTable(ivtbl.first.first));
      
      //get the old v table 
      ConstantArray* vtable = cha->getOldVTable(ivtbl.first.first);

      //get the ivtbl.second operand of this v table 
      Constant* constant = vtable->getOperand(ivtbl.second);

      // get the v thunk function for constant c
      Function* thunk = getVthunkFunction(constant);
      
      //if not null 
      if (thunk) {

        //create a new thunk function with a new name based on thunk and the parent class name 
        Function* newThunk = M.getFunction(NEW_VTHUNK_NAME(thunk, cha->getLayoutClassName(ivtbl.first)));
        assert(newThunk);
        
        //create a new bit cast constant using the newthunk and the context Context
        Constant* newC = ConstantExpr::getBitCast(newThunk, IntegerType::getInt8PtrTy(Context));

        //add the new constant to the new v table elements 
        newVtableElems.push_back(newC);

        //insert the intermeadiate thunk for removal 
        vthunksToRemove.insert(thunk);
      } else {

        //else just add the constant 
        newVtableElems.push_back(constant);
      }
    }
  }
  
  /*
  start creating the new global variables witht the new v table layouts inside  
  */

  // create the constant initializer
  Constant* newVtableInit = ConstantArray::get(newArrType, newVtableElems);

  // create the new v global variable which will be used to replace the old one
  GlobalVariable* newGlobalVariable = new GlobalVariable(M,
                                        newArrType, 
                                              true,
                   GlobalVariable::InternalLinkage,
                    nullptr, NEW_VTABLE_NAME(vtbl)); // give new v table name, NEW_VTABLE_NAME(vtbl) ("_SD" + vtbl)

  assert(alignmentMap.count(vtbl));

  // compute the new v table alignment
  // this will be put inside the new v table 
  // The new v table will be added in the end in the new Constant
  newGlobalVariable->setAlignment(alignmentMap[vtbl]);

  //set initializer 
  newGlobalVariable->setInitializer(newVtableInit);

  //set unnamed address to true 
  newGlobalVariable->setUnnamedAddr(true);

  //attach "_SD" in the name of the new v table 
  cloudStartMap[NEW_VTABLE_NAME(vtbl)] = newGlobalVariable;

  // to start changing the original uses of the vtables, 
  // first get all the classes in the cloud
  // these are all the nodes associated to a root node contained
  // in the roots vector. Get the nodes in preorder traversal
  // for the root node vtbl 
  order_t cloudPreorderNodes = cha->preorder(vtbl_t(vtbl, 0));
  
  //declare a new zero constant 
  Constant* zero = ConstantInt::get(M.getContext(), APInt(64, 0));

  //iterate thorugh all nodes 
  for (const vtbl_t& vnode : cloudPreorderNodes) {
    
    //check if node is defined
    if (cha->isDefined(vnode)) {
      assert(newVTableStartAddrMap.find(vnode) == newVTableStartAddrMap.end());
      newVTableStartAddrMap[vnode] = newVtblAddressConst(M, vnode);
    }
    
    //if node is undefined skip
    if (cha->isUndefined(vnode.first))
      continue;

    // get the original global variable holding the v table
    GlobalVariable* globalVar = M.getGlobalVariable(vnode.first, true);
    assert(globalVar);

    // since we change the collection while we're iterating it,
    // put into users all the uses of the global variable 
    std::set<User*> users(globalVar->user_begin(), globalVar->user_end());

    // iterate though all the uses of the global variable 
    for (std::set<User*>::iterator userItr = users.begin(); userItr != users.end(); userItr++) {
     
      // this should be a constant getelementptr
      User* user = *userItr; //user iterator
      assert(user);

      //cast user to const. expression
      ConstantExpr* userCE = dyn_cast<ConstantExpr>(user);

      //check that user const. expression is == GEP_OPCODE == 29
      //GEP = get element pointer 
      assert(userCE && userCE->getOpcode() == GEP_OPCODE);

      // get the address pointer from the instruction
      ConstantInt* oldConst = dyn_cast<ConstantInt>(userCE->getOperand(2));
      assert(oldConst);
      uint64_t oldAddrPt = oldConst->getSExtValue();

      // find which part of the vtable the constructor uses
      assert(cha->hasAddrPt(vnode.first, oldAddrPt));
      uint64_t order = cha->getAddrPtOrder(vnode.first, oldAddrPt);

      // if this is not referring to the current part, continue
      if (order != vnode.second)
        continue;

      // find the offset relative to the sub-vtable start
      int addrInsideBlock = oldAddrPt - cha->getRange(vnode.first, order).first;

      // find the new offset corresponding to the relative offset
      // inside the interleaved vtable
      int64_t newAddrPt = newLayoutInds[vnode][addrInsideBlock];
      
      //declare a new offset constant 
      Constant* newOffsetConstant  = ConstantInt::getSigned(Type::getInt64Ty(M.getContext()), newAddrPt);
      
      //declare a new array of indices
      std::vector<Constant*> indices;

      //insert the zero constant 
      indices.push_back(zero);

      //insert the new offset constant 
      indices.push_back(newOffsetConstant);
      
      //this is just an example of how to declare a Constant 
      /*Constant *ConstantExpr::getGetElementPtr(Type *Ty, 
                                              Constant *C,
                                   ArrayRef<Value *> Idxs, 
                                            bool InBounds,
                                     Type *OnlyIfReducedTy)
      */
      
      //this is the new constant which will be inserted containing the new v tables 
      Constant* newConstExpr = ConstantExpr::getGetElementPtr(newArrType, //ArrayType 
                                                       newGlobalVariable, //GlobalVariable
                                                                 indices, //std::vector<Constant*>
                                                                   true); //bool inBounds
      
      // replace in the user constant expression 
      // with the one that uses the new vtable, newConstExpr
      userCE->replaceAllUsesWith(newConstExpr);
     
      // remove the user constant expression 
      userCE->destroyConstant();
    }
  }
}

/*Paul:
check if lhs (left hand side) it is less equal rhs (right hand side)*/
static bool sd_isLE(int64_t lhs, int64_t rhs) { return lhs <= rhs; }

/*Paul:
check if lhs (left hand side) it is greather equal rhs (right hand side)*/
static bool sd_isGE(int64_t lhs, int64_t rhs) { return lhs >= rhs; }

//Paul: this is used to fill (with positive and negative part) the interleaving map with the rest of the component
//after the interleaving was performed 
void SDLayoutBuilder::fillVtablePart(SDLayoutBuilder::interleaving_list_t& vtblPartList, 
                                              const SDLayoutBuilder::order_t& nodesInPreorder, 
                                                                    bool positivePartOn_Off) {
  std::map<vtbl_t, int64_t> posMap;     // current position
  std::map<vtbl_t, int64_t> lastPosMap; // last possible position
 
  //Paul: traverse only the ones from this child 
  // This is ok. 
  for(const vtbl_t& n : nodesInPreorder) {
    uint64_t addrPt = cha->addrPt(n);  // get the address point of the vtable
    const range_t &r = cha->getRange(n); // get the range (start & end address) of that particular v table 
    posMap[n]     = positivePartOn_Off ? addrPt : (addrPt - 1); // position map = addrPt or addrPt - 1
    lastPosMap[n] = positivePartOn_Off ? r.second : (r.first - prePadMap[n]); //set last position map 
  }

  interleaving_list_t current; // interleaving of one element
  bool (*check)(int64_t,int64_t) = positivePartOn_Off ? sd_isLE : sd_isGE;//Paul: use one or the other check 
  int increment = positivePartOn_Off ? 1 : -1; //use either 1 or -1
  int64_t pos;

  // while we have an element to insert to the vtable, continue looping
  while(true) {
    // traverse all the nodes from the hierarchy 
    for (const vtbl_t& n : nodesInPreorder) {
      pos = posMap[n];
      if (!cha->isUndefined(n.first) && check(pos, lastPosMap[n])) {
        //Paul: add all the nodes from this hierarchy n == node == v table to a certain position 
        current.push_back(interleaving_t(n, pos));
        posMap[n] += increment; // this adds 1 or substract -1 from the position map for each of the nodes 
      }
    }

    if (current.size() == 0)
      break;

    // declare an interator to the interleaving list pointing at the beginning or the end of the interleaving list 
    interleaving_list_t::iterator itr = positivePartOn_Off ? vtblPartList.end() : vtblPartList.begin();

    //Paul: in the end add the results contained in current interleaving list baset on the iterator itr
    // (at the end or beginning) of the final interleaving list 
    vtblPartList.insert(itr, current.begin(), current.end());

    current.clear();
  }
}

//Paul: compute the new translated v table index 
int64_t SDLayoutBuilder::translateVtblInd(SDLayoutBuilder::vtbl_t vname, int64_t offset, bool isRelative = true) {

  if (cha->isUndefined(vname) && cha->hasFirstDefinedChild(vname)) {
    vname = cha->getFirstDefinedChild(vname);
  }

  if (!newLayoutInds.count(vname)) {
    sd_print("Vtbl %s %d, undefined: %d.\n",
        vname.first.c_str(), vname.second, cha->isUndefined(vname));
    sd_print("has first child %d.\n", cha->hasFirstDefinedChild(vname));
    
    if (cha->knowsAbout(vname) && cha->hasFirstDefinedChild(vname)) {
      sd_print("class: (%s, %lu) doesn't belong to newLayoutInds\n", vname.first.c_str(), vname.second);
      sd_print("%s has %u address points\n", vname.first.c_str(), cha->getNumAddrPts(vname.first));
      
      for (uint64_t i = 0; i < cha->getNumAddrPts(vname.first); i++)
        sd_print("  addrPt: %d\n", cha->addrPt(vname.first, i));
      assert(false);
    }
    
    //return the old offset 
    return offset;
  }
  
  //there exists a range for the v table name 
  assert(cha->hasRange(vname));

  //get new layouts indices for this new v table name 
  std::vector<uint64_t>& newInds = newLayoutInds[vname];

  //get the range for the v table v name 
  const range_t& subVtableRange = cha->getRange(vname);
  
  //check if is relative call 
  if (isRelative) {

    //compute old address point 
    int64_t oldAddrPt = cha->addrPt(vname) - subVtableRange.first;

    //full index is oldad address point + offset 
    int64_t fullIndex = oldAddrPt + offset;
    
    //if fullIndex is negative and less equal the difference between sub v table range second and sub v table range first 
    // return an error in the translated v table index 
    if (! (fullIndex >= 0 && fullIndex <= (int64_t) (subVtableRange.second - subVtableRange.first))) {
      sd_print("error in translateVtblInd: %s, addrPt:%ld, old:%ld\n", vname.first.c_str(), oldAddrPt, offset);
      assert(false);
    }
    
    //return the new index as the difference between the fullIndex and old address point 
    return ((int64_t) newInds.at(fullIndex)) - ((int64_t) newInds.at(oldAddrPt));

    //if not relative 
  } else {
    //offset >= 0 and offset <= new indices size()
    assert(0 <= offset && offset <= (int64_t)newInds.size());

    //return the new index 
    return newInds[offset];
  }
}

//get the v table range start 
llvm::Constant* SDLayoutBuilder::getVTableRangeStart(const SDLayoutBuilder::vtbl_t& vtbl) {
  return newVTableStartAddrMap[vtbl];
}

/*Paul:
as usual, after the analysis is done clear all the 
used data structures*/
void SDLayoutBuilder::clearAnalysisResults() {
  cha->clearAnalysisResults();
  newLayoutInds.clear();
  interleavingMap.clear();

  sd_print("Cleared SDLayoutBuilder analysis results \n");
}

/// ----------------------------------------------------------------------------
/// SDChangeIndices implementation
/// ----------------------------------------------------------------------------
/*Paul:
since the v table layout is stored in the metadata which
is attached to an LLVM GlobalVariable this has to erased from the 
parent in order to be replaced with the new metadata which we will generate.
The result of the whole interleaving and reordering analysis is just the
new metadata which will be put back in place of the older one*/

void SDLayoutBuilder::removeOldLayouts(Module &M) {
 
  //collect GV and than remove from parent
  for (auto itr = cha->oldVTables_begin(); itr != cha->oldVTables_end(); itr ++) {
    GlobalVariable* var = M.getGlobalVariable(itr->first, true);
    assert(var && var->use_empty());
    sd_print("Deleting vtbl: %s \n", var->getName().data());
    var->eraseFromParent();
  }

  // remove all original v thunks from the module
  while (! vthunksToRemove.empty()) {
    Function* f = * vthunksToRemove.begin();
    vthunksToRemove.erase(f);
    
    //erase the Function from the parent
    f->eraseFromParent();
  }

  //collect and than remove
  for (Module::FunctionListType::iterator itr = M.getFunctionList().begin();
       itr != M.getFunctionList().end(); itr++){

    //sd_isVthunk checks that the name contains the substrings "_ZTcv" or "_ZTc""
    if (sd_isVthunk(itr->getName()) && (itr->user_begin() == itr->user_end())) {
      vthunksToRemove.insert(itr); //collect the v thunks 
    }
  }

  // remove all original thunks from the module
  while (! vthunksToRemove.empty()) {
    Function* f = * vthunksToRemove.begin();
    vthunksToRemove.erase(f);
    f->eraseFromParent();
  }
}

/**
 * New starting address point inside the interleaved vtable
 */
uint64_t SDLayoutBuilder::newVtblAddressPoint(const vtbl_name_t& name) {
  vtbl_t vtbl(name,0);
  assert(newLayoutInds.count(vtbl));
  return newLayoutInds[vtbl][0];
}

/**
 * Get the vtable address of the class in the interleaved scheme,
 * and insert the necessary instructions before the given instruction
 */
Value* SDLayoutBuilder::newVtblAddress(Module& M, const vtbl_name_t& name, Instruction* inst) {
  vtbl_t vtbl(name,0);

  assert(cha->hasAncestor(vtbl));
  vtbl_name_t rootName = cha->getAncestor(vtbl);

  // sanity checks
  assert(cha->isRoot(rootName));

  // switch to the new vtable name
  rootName = NEW_VTABLE_NAME(rootName);

  // we should add the address point of the given class
  // inside the new interleaved vtable to the start address
  // of the vtable

  // find which element is the address point
  assert(cha->getNumAddrPts(name));
  unsigned addrPt = cha->addrPt(name, 0);

  // now find its new index
  assert(newLayoutInds.count(vtbl));
  uint64_t addrPtOff = newLayoutInds[vtbl][addrPt];

  // this should exist already
  GlobalVariable* gv = M.getGlobalVariable(rootName);
  assert(gv);

  // create a builder to insert the add, etc. instructions
  IRBuilder<> builder(inst);
  builder.SetInsertPoint(inst);

  LLVMContext& C = M.getContext();
  IntegerType* type = IntegerType::getInt64Ty(C);

  // add the offset to the beginning of the vtable
  Value* vtableStart   = builder.CreatePtrToInt(gv, type);
  Value* offsetVal     = ConstantInt::get(type, addrPtOff * WORD_WIDTH);
  Value* vtableAddrPtr = builder.CreateAdd(vtableStart, offsetVal);

  return vtableAddrPtr;
}

/*Paul:
create a new v table address constant LLVM variable*/
Constant* SDLayoutBuilder::newVtblAddressConst(Module& M, const vtbl_t& vtbl) {
  const DataLayout &DL = M.getDataLayout();
  assert(cha->hasAncestor(vtbl));
  vtbl_name_t rootName = cha->getAncestor(vtbl);
  LLVMContext& C = M.getContext();
  Type *IntPtrTy = DL.getIntPtrType(C);

  // sanity checks
  assert(cha->isRoot(rootName));

  // switch to the new vtable name
  rootName = NEW_VTABLE_NAME(rootName);

  // we should add the address point of the given class
  // inside the new interleaved vtable to the start address
  // of the vtable

  // find which element is the address point
  assert(cha->getNumAddrPts(vtbl.first));
  unsigned addrPt = cha->addrPt(vtbl) - cha->getRange(vtbl).first;

  // now find its new index
  assert(newLayoutInds.count(vtbl));
  uint64_t addrPtOff = newLayoutInds[vtbl][addrPt];

  // this should exist already
  GlobalVariable* gv = cloudStartMap[rootName];
  assert(gv);

  // add the offset to the beginning of the vtable
  Constant* gvInt     = ConstantExpr::getPtrToInt(gv, IntPtrTy);
  Constant* offsetVal = ConstantInt::get(IntPtrTy, addrPtOff * WORD_WIDTH);
  Constant* gvOffInt  = ConstantExpr::getAdd(gvInt, offsetVal);

  return gvOffInt;
}

/** Paul: 
    This is the main function of this pass. 
    After the clouds have been generated the info
    will be attacked to new global variables. 
    These variables will be created by us.

 * Interleave the generated clouds and create a new global variable for each of them.
 */
void SDLayoutBuilder::buildNewLayouts(Module &M) {

  sd_print("CHA cloud map has %d root nodes \n", cha->getNumberOfRoots());
  
  //1: we iterate through all roots contained in the cloud, order or interleave them 
  for (auto itr = cha->roots_begin(); itr != cha->roots_end(); itr++) {
   
    vtbl_name_t vtbl = *itr;         // get the v table name as string
 
    //Paul: interleave or order for each v table separatelly 
    if (interleave){
      //interleaveCloud(vtbl);         // interleave the cloud or

      //our interleaving method 
      interleaveCloudNew(vtbl);         // interleave the cloud or

    }else{
      orderCloud(vtbl);              // order the cloud
    }
    
    // Paul: we can create a new algorithm which is a combination of the interleaving and ordering algorithms
    // The algorithm should remove the disadvantages of both of these algorithms and it should carefully 
    // filter out v tables which are not the v table ancestor path 


    //Paul: calculate the new layout indices
    // the new indices will be used when inserting the new v table layouts inside the metadata.
    // Inside this method the interleavedMap obtained in the interleaveCloud or 
    // orderCloud will be used to compute the new index of the v table. 
    // This is just a simple counting and ssigning an index number to the new elements.
    calculateNewLayoutInds(vtbl);    // calculate the new indices from the interleaved vtable
  }
  
  //2: we iterate through all roots contained in the cloud and replace 
  //v thunks and emit global variables.
  for (auto itr = cha->roots_begin(); itr != cha->roots_end(); itr++) {

    // get the v table name as string
    vtbl_name_t vtbl = *itr;        
    
    // create new thunk function and add it to M.getFunctionList().push_back(newThunkF);
    // replace the old v pointer whith the new one using Intrinsics::sd_vcall_indexF
    createThunkFunctions(M, vtbl); 

    // emit the new global variables with the new v tables inside.  
    // Previously that mens that the v tables where extended with
    // all v table children of a given root node. This means that to many v tables are attached 
    // to a Global Variable. This is bad! (attack surface is increased).
    // Note: the added range checks reflect the contents of this global variable 
    createNewVTable(M, vtbl);        
  }

  // 3: we iterate through all roots contained in the cloud and 
  // calculate v pointer ranges and than verify the v pointer ranges
  for (auto itr = cha->roots_begin(); itr != cha->roots_end(); itr++) {
    vtbl_name_t vtbl = *itr;  // get the v table name as string
    
    //calculate the v ptr ranges, these will added into the checks.
    //this ranges have to be the most restrictive as posible and precise.
    //There is at the moment no better way as considering the object base class 
    //and the base class of the function which the object is calling, see SW paper.
    calculateVPtrRanges(M, vtbl);  

    //Check that the ranges of the descendants are disjoint:
    //1.This means they do not overlap at all.
    //2.Check that each descendent is in one of the ranges. 
    verifyVPtrRanges(vtbl);         
  }
}

