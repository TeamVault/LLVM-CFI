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
the new layout is ok, as expected)
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
check that the v table layouts are ok (match with the old ones)*/
bool SDLayoutBuilder::verifyNewLayouts(Module &M) {
  new_layout_inds_map_t indMap;
  for (auto vtblIt = cha->roots_begin(); vtblIt != cha->roots_end(); vtblIt++) { 
    vtbl_name_t vtbl = *vtblIt;
    vtbl_t root(vtbl, 0);
    interleaving_list_t &interleaving = interleavingMap[vtbl];
    uint64_t i = 0;

    indMap.clear();
  
    std::cerr << "Verifying cloud : " << vtbl << "\n";
    // Build a map (vtbl_t -> (uint64_t -> uint64_t)) with the old-to-new index mapping encoded in the
    // interleaving
    for (auto elem : interleaving) {
      vtbl_t &v = elem.first;
      uint64_t oldPos = elem.second;

      if (v == dummyVtable)
        continue;

      if (indMap.find(v) == indMap.end()) {
        indMap[v] = std::map<uint64_t, uint64_t>();
      } else {
        if (indMap[v].count(oldPos) != 0) {
          std::cerr << "In ivtbl " << vtbl << " entry " << v.first << "," << v.second << "[" << oldPos << "]"
            << " appears twice - at " << indMap[v][oldPos] << " and " << i << std::endl;
          dumpNewLayout(interleaving);
          return false;
        }
      }

      indMap[v][oldPos] = i;
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
          dumpNewLayout(interleaving);
          return false;
      }

      if (indMap[n].size() != oldVtblSize) {
          std::cerr << "In ivtbl " << vtbl << " index mapping for " << n.first << "," << n.second << 
            " has " << indMap[n].size() << " expected " << oldVtblSize << std::endl;
          dumpNewLayout(interleaving);
          return false;
      }
    }

    if (!interleave)
      return true;

    // 1.5) Check that for each parent/child the child is contained in the parent
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

        if ((ptAddrPt - ptStart + prePadMap[pt]) > 
            (childAddrPt - childStart + prePadMap[*child]) ||
            ptEnd - ptAddrPt > childEnd - childAddrPt) {
              
          sd_print("Parent vtable(%s,%d) [%d-%d,%d,%d] is not contained in child vtable(%s,%d) [%d-%d,%d,%d]",
              pt.first.c_str(), pt.second, ptStart, prePadMap[pt],ptAddrPt, ptEnd, 
              child->first.c_str(), child->second, childStart, prePadMap[*child], childAddrPt, childEnd);
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
            sd_print("Parent (%s,%d) old relative index %d(new relative %d) mismatches child(%s,%d) corresponding old index %d(new relative %d)",
                pt.first.c_str(), pt.second, (ind - ptAddrPt), newPtInd,
                child->first.c_str(), child->second, ind + ptToChildAdj - childAddrPt, newChildInd);
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
  if ((bcExpr = dyn_cast<ConstantExpr>(vtblElement)) && bcExpr->getOpcode() == BITCAST_OPCODE) {
    Constant* operand = bcExpr->getOperand(0);

    // this is a vthunk
    if (sd_isVthunk(operand->getName())) {
      Function* thunkF = dyn_cast<Function>(operand);
      assert(thunkF);
      return thunkF;
    }
  }

  return NULL;
}

void SDLayoutBuilder::createThunkFunctions(Module& M, const vtbl_name_t& rootName) {
  // for all defined vtables
  vtbl_t root(rootName,0);
  order_t vtbls = cha->preorder(root);

  LLVMContext& C = M.getContext();

  Function *sd_vcall_indexF =
      M.getFunction(Intrinsic::getName(Intrinsic::sd_get_vcall_index));

  for (unsigned i=0; i < vtbls.size(); i++) {
    const vtbl_name_t& vtbl = vtbls[i].first;
    if (!cha->hasOldVTable(vtbl)) {
      assert(cha->isUndefined(vtbl));
      continue;
    }

    ConstantArray* vtableArr = cha->getOldVTable(vtbl);

    // iterate over the vtable elements
    for (unsigned vtblInd = 0; vtblInd < vtableArr->getNumOperands(); ++vtblInd) {
      Constant* c = vtableArr->getOperand(vtblInd);
      Function* thunkF = getVthunkFunction(c);
      if (! thunkF)
        continue;

      // find the index of the sub-vtable inside the whole
      unsigned order = cha->getVTableOrder(vtbl, vtblInd);

      // this should have a parent
      const std::string& parentClass = cha->getLayoutClassName(vtbl, order);
      std::string newThunkName(NEW_VTHUNK_NAME(thunkF, parentClass));

      if (M.getFunction(newThunkName)) {
        // we already created such function, will use that later
        continue;
      }

      // duplicate the function and rename it
      ValueToValueMapTy VMap;
      Function *newThunkF = llvm::CloneFunction(thunkF, VMap, false);
      newThunkF->setName(newThunkName);
      M.getFunctionList().push_back(newThunkF);

      CallInst* CI = NULL;

      if(sd_vcall_indexF == NULL)
        continue;

      // go over its instructions and replace the one with the metadata
      for(Function:: iterator bb_itr = newThunkF->begin(); bb_itr != newThunkF->end(); bb_itr++) {
        for(BasicBlock:: iterator i_itr = bb_itr->begin(); i_itr != bb_itr->end(); i_itr++) {
          Instruction* inst = i_itr;

          if ((CI = dyn_cast<CallInst>(inst)) && CI->getCalledFunction() == sd_vcall_indexF) {
            // get the arguments
            llvm::ConstantInt* oldVal = dyn_cast<ConstantInt>(CI->getArgOperand(0));
            assert(oldVal);

            // extract the old index
            int64_t oldIndex = oldVal->getSExtValue() / WORD_WIDTH;

            // calculate the new one
            //sd_print("Create thunk function %s\n", newThunkName.c_str());
            int64_t newIndex = translateVtblInd(vtbl_t(vtbl,order), oldIndex, true);

            Value* newValue = ConstantInt::get(IntegerType::getInt64Ty(C), newIndex * WORD_WIDTH);

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
  sd_print("Started ordering for one vtable ...\n");

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
  
  sd_print("Finishing ordering for one vtable ...\n");
}

/*Paul: 
this function is used to interleave the cloud.
The interleaving can be shut down and it is not dependent of
the ordering operation from above
*/
void SDLayoutBuilder::interleaveCloud(SDLayoutBuilder::vtbl_name_t& vtbl) {
  sd_print("Started Interleaving for one v table (root) ...\n");
  
  /*Paul:
  check that the v table is a root in a subcloud of the main cloud. 
  The CHA pass has previously generated a cloud. 
  Each subcloud of the cloud has a root.
  */
  assert(cha->isRoot(vtbl));

  // create a temporary list for the positive part
  interleaving_list_t positivePart;

  vtbl_t root(vtbl,0);
  order_t pre = cha->preorder(root);
  std::map<vtbl_t, uint64_t> indMap;
  for (uint64_t i = 0; i < pre.size(); i++)
    indMap[pre[i]] = i;

  // First check if any vtable needs pre-padding. (All vtables must contain their parents).
  for (auto pt : pre) {
    if (cha->isUndefined(pt))
      continue; 

    for (auto child = cha->children_begin(pt); child != cha->children_end(pt); child++) {
        if (cha->isUndefined(pt))
          continue; 

        if (indMap[*child] < indMap[pt])
          continue; // Earlier in the preorder traversal - visited from a different node.

        const range_t &ptR = cha->getRange(pt);
        const range_t &chR = cha->getRange(*child);

        uint64_t ptStart = ptR.first;
        uint64_t ptEnd = ptR.second;
        uint64_t ptAddrPt = cha->addrPt(pt);
        uint64_t childStart = chR.first;
        uint64_t childEnd = chR.second;
        uint64_t childAddrPt = cha->addrPt(*child);

        uint64_t ptPreAddrPt = ptAddrPt - ptStart + prePadMap[pt];
        uint64_t childPreAddrPt = childAddrPt - childStart + prePadMap[*child];

        prePadMap[*child] = (ptPreAddrPt > childPreAddrPt ?
          ptPreAddrPt - childPreAddrPt : prePadMap[*child]);
    }
  }

  // initialize the cloud's interleaving list
  interleavingMap[vtbl] = interleaving_list_t();

  // fill both parts
  fillVtablePart(interleavingMap[vtbl], pre, false);
  fillVtablePart(positivePart, pre, true);

  // append positive part to the negative
  interleavingMap[vtbl].insert(interleavingMap[vtbl].end(), positivePart.begin(), positivePart.end());
  alignmentMap[vtbl] = WORD_WIDTH;
  
  sd_print("Finishing Interleaving for one v table (root)...\n");
}

/*Paul:
calculate the new layout indices
*/
void SDLayoutBuilder::calculateNewLayoutInds(SDLayoutBuilder::vtbl_name_t& vtbl){
  
  assert(interleavingMap.count(vtbl));

  uint64_t currentIndex = 0;
  for (const interleaving_t& ivtbl : interleavingMap[vtbl]) {
    //sd_print("NewLayoutInds for vtable (%s,%d)\n", ivtbl.first.first.c_str(), ivtbl.first.second);
    if(ivtbl.first != dummyVtable) {
      // record the new index of the vtable element coming from the current vtable
      newLayoutInds[ivtbl.first].push_back(currentIndex++);
    } else {
      currentIndex++;
    }
  }
}

/*Paul:
this is a helper function for the v pointer range calculator 
*/
void SDLayoutBuilder::calculateVPtrRangesHelper(const SDLayoutBuilder::vtbl_t& vtbl, std::map<vtbl_t, uint64_t> &indMap){
  // Already computed
  if (rangeMap.find(vtbl) != rangeMap.end())
    return;

  for (auto childIt = cha->children_begin(vtbl); childIt != cha->children_end(vtbl); childIt++) {
    const vtbl_t &child = *childIt;
    calculateVPtrRangesHelper(child, indMap);
  }

  std::vector<range_t> ranges;

  ranges.push_back(range_t(indMap[vtbl], indMap[vtbl]+1));

  for (auto childIt = cha->children_begin(vtbl); childIt != cha->children_end(vtbl); childIt++) {
    const vtbl_t &child = *childIt;
    ranges.insert(ranges.end(), rangeMap[child].begin(), rangeMap[child].end());
  }

  std::sort(ranges.begin(), ranges.end());

  // Coalesce ranges
  std::vector<range_t> coalesced_ranges;
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

  /*
  std::cerr << "{" << vtbl.first << "," << vtbl.second << "} From ranges [";
  for (auto it : ranges)
    std::cerr << "(" << it.first << "," << it.second << "),";
  std::cerr << "] coalesced [";
  for (auto it : coalesced_ranges)
    std::cerr << "(" << it.first << "," << it.second << "),";
  std::cerr << "]\n";
  */
  rangeMap[vtbl] = coalesced_ranges;
}

/*Paul:
final step of the analysis after the v pointer ranges have been generated*/
void SDLayoutBuilder::verifyVPtrRanges(SDLayoutBuilder::vtbl_name_t& vtbl){
  SDLayoutBuilder::vtbl_t root(vtbl, 0);
  order_t pre = cha->preorder(root);
  std::map<vtbl_t, uint64_t> indMap;
  std::map<vtbl_t, order_t> descendantsMap;
  for (uint64_t i = 0; i < pre.size(); i++) {
    indMap[pre[i]] = i;
    descendantsMap[pre[i]] = cha->preorder(pre[i]);
  }

  for (auto v : descendantsMap) {
    uint64_t totalRange = 0;
    int64_t lastEnd = -1;

    // Check ranges are disjoint
    for (auto range : rangeMap[v.first]) {
      totalRange += range.second - range.first;
      assert(lastEnd < ((int64_t)range.first));
      lastEnd = range.second;
    }

    // Sum of ranges length equals the number of descendents
    assert(totalRange == v.second.size());

    // Each descendent is in one of the ranges.
    for (auto descendant : v.second) {
      uint64_t ind = indMap[descendant];
      bool found = false;
      for (auto range : rangeMap[v.first]) {
        if (range.first <= ind && range.second > ind) {
          found = true;
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
const std::vector<SDLayoutBuilder::mem_range_t>& 
SDLayoutBuilder::getMemRange(const vtbl_t &vtbl) {
  return memRangeMap[vtbl];
}

/*Paul:
calculate the v pointer ranges which will be used to constrain each
v call site*/
void SDLayoutBuilder::calculateVPtrRanges(Module& M, SDLayoutBuilder::vtbl_name_t& vtbl){
  SDLayoutBuilder::vtbl_t root(vtbl, 0); // Paul: declare a v table with name vtbl and index 0

  order_t pre = cha->preorder(root); //Paul: do a preorder traversal with the newly created root v table

  std::map<vtbl_t, uint64_t> indMap;
  for (uint64_t i = 0; i < pre.size(); i++) indMap[pre[i]] = i;

  calculateVPtrRangesHelper(root, indMap);

  for (uint64_t i = 0; i < pre.size(); i++) {
//    std::cerr << "For " << pre[i].first << "," << pre[i].second << " ";

    for (auto it : rangeMap[pre[i]]) {
      uint64_t start = it.first,
               end = it.second,
               def_count = 0;


      for (int j = start; j < end; j++)
        if (cha->isDefined(pre[j])) def_count++;
/*
      std::cerr << "(range " << start << "-" << end << " contains "
        << def_count << " defined,";
*/
      if (def_count == 0)
        continue;

      while (cha->isUndefined(pre[start]) && start < end) {
//        std::cerr << "skipping " << pre[start].first << "," << pre[start].second 
//          << ",";
        start++;
      }
/*
      std::cerr << "final range " << pre[start].first << "," << pre[start].second
        << "+" << def_count << ")";
*/
      memRangeMap[pre[i]].push_back(mem_range_t(newVtblAddressConst(M, pre[start]), def_count));
    }
    //std::cerr << "\n";
  }
}

/*Paul
this method creates a new v table which will be added to a new constant.
The new constant will replace the old one in the end of this method.
*/
void SDLayoutBuilder::createNewVTable(Module& M, SDLayoutBuilder::vtbl_name_t& vtbl){
  // get the interleaved order
  interleaving_list_t& newVtbl = interleavingMap[vtbl];

  // calculate the global variable type
  uint64_t newSize = newVtbl.size();
  PointerType* vtblElemType = PointerType::get(IntegerType::get(M.getContext(), WORD_WIDTH), 0);
  ArrayType* newArrType = ArrayType::get(vtblElemType, newSize);

  LLVMContext& C = M.getContext();

  // fill the interleaved vtable element list
  std::vector<Constant*> newVtableElems;
  for (const interleaving_t& ivtbl : newVtbl) {
    const range_t &r = cha->getRange(ivtbl.first);
    if (cha->isUndefined(ivtbl.first.first) || ivtbl.first == dummyVtable || ivtbl.second < r.first) {
      newVtableElems.push_back(
        Constant::getNullValue(IntegerType::getInt8PtrTy(C)));
    } else {
      assert(cha->hasOldVTable(ivtbl.first.first));

      ConstantArray* vtable = cha->getOldVTable(ivtbl.first.first);
      Constant* c = vtable->getOperand(ivtbl.second);
      Function* thunk = getVthunkFunction(c);

      if (thunk) {
        Function* newThunk = M.getFunction(
              NEW_VTHUNK_NAME(thunk, cha->getLayoutClassName(ivtbl.first)));
        assert(newThunk);

        Constant* newC = ConstantExpr::getBitCast(newThunk,
          IntegerType::getInt8PtrTy(C));
        newVtableElems.push_back(newC);
        vthunksToRemove.insert(thunk);
      } else {
        newVtableElems.push_back(c);
      }
    }
  }

  // create the constant initializer
  Constant* newVtableInit = ConstantArray::get(newArrType, newVtableElems);

  // create the global variable
  // thi variable will replace the old global variable
  GlobalVariable* newVtable = new GlobalVariable(M, newArrType, true,
                                                 GlobalVariable::InternalLinkage,
                                                 nullptr, NEW_VTABLE_NAME(vtbl));
  assert(alignmentMap.count(vtbl));
  newVtable->setAlignment(alignmentMap[vtbl]);
  newVtable->setInitializer(newVtableInit);
  newVtable->setUnnamedAddr(true);

  cloudStartMap[NEW_VTABLE_NAME(vtbl)] = newVtable;

  // to start changing the original uses of the vtables, first get all the classes in the cloud
  order_t cloud = cha->preorder(vtbl_t(vtbl, 0));

  Constant* zero = ConstantInt::get(M.getContext(), APInt(64, 0));
  for (const vtbl_t& v : cloud) {
    if (cha->isDefined(v)) {
      assert(newVTableStartAddrMap.find(v) == newVTableStartAddrMap.end());
      newVTableStartAddrMap[v] = newVtblAddressConst(M, v);
    }

    if (cha->isUndefined(v.first))
      continue;

    // find the original vtable
    GlobalVariable* globalVar = M.getGlobalVariable(v.first, true);
    assert(globalVar);

    // since we change the collection while we're iterating it,
    // put the users into a separate set first
    std::set<User*> users(globalVar->user_begin(), globalVar->user_end());

    // replace the uses of the original vtables
    for (std::set<User*>::iterator userItr = users.begin(); userItr != users.end(); userItr++) {
      // this should be a constant getelementptr
      User* user = *userItr;
      assert(user);
      ConstantExpr* userCE = dyn_cast<ConstantExpr>(user);
      assert(userCE && userCE->getOpcode() == GEP_OPCODE);

      // get the address pointer from the instruction
      ConstantInt* oldConst = dyn_cast<ConstantInt>(userCE->getOperand(2));
      assert(oldConst);
      uint64_t oldAddrPt = oldConst->getSExtValue();

      // find which part of the vtable the constructor uses
      assert(cha->hasAddrPt(v.first, oldAddrPt));
      uint64_t order = cha->getAddrPtOrder(v.first, oldAddrPt);

      // if this is not referring to the current part, continue
      if (order != v.second)
        continue;

      // find the offset relative to the sub-vtable start
      int addrInsideBlock = oldAddrPt - cha->getRange(v.first, order).first;

      // find the new offset corresponding to the relative offset
      // inside the interleaved vtable
      int64_t newAddrPt = newLayoutInds[v][addrInsideBlock];

      Constant* newOffsetCons  = ConstantInt::getSigned(Type::getInt64Ty(M.getContext()), newAddrPt);

      std::vector<Constant*> indices;
      indices.push_back(zero);
      indices.push_back(newOffsetCons);

      Constant* newConst = ConstantExpr::getGetElementPtr(newArrType, newVtable, indices, true);
      // replace the constant expression with the one that uses the new vtable
      userCE->replaceAllUsesWith(newConst);
      // and then remove it
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

void SDLayoutBuilder::fillVtablePart(SDLayoutBuilder::interleaving_list_t& vtblPart, const SDLayoutBuilder::order_t& order, bool positiveOff) {
  std::map<vtbl_t, int64_t> posMap;     // current position
  std::map<vtbl_t, int64_t> lastPosMap; // last possible position

  for(const vtbl_t& n : order) {
    uint64_t addrPt = cha->addrPt(n);  // get the address point of the vtable
    const range_t &r = cha->getRange(n);
    posMap[n]     = positiveOff ? addrPt : (addrPt - 1); // start interleaving from that address
    lastPosMap[n] = positiveOff ? r.second : (r.first - prePadMap[n]);
  }

  interleaving_list_t current; // interleaving of one element
  bool (*check)(int64_t,int64_t) = positiveOff ? sd_isLE : sd_isGE;
  int increment = positiveOff ? 1 : -1;
  int64_t pos;

  // while we have an element to insert to the vtable, continue looping
  while(true) {
    // do a preorder traversal and add the remaining elements
    for (const vtbl_t& n : order) {
      pos = posMap[n];
      if (!cha->isUndefined(n.first) && check(pos, lastPosMap[n])) {
        current.push_back(interleaving_t(n, pos));
        posMap[n] += increment;
      }
    }

    if (current.size() == 0)
      break;

    // for positive offset, append the current interleaved part to the end
    // otherwise, insert to the front
    interleaving_list_t::iterator itr =
        positiveOff ? vtblPart.end() : vtblPart.begin();

    vtblPart.insert(itr, current.begin(), current.end());

    current.clear();
  }
}

int64_t SDLayoutBuilder::translateVtblInd(SDLayoutBuilder::vtbl_t name, int64_t offset, bool isRelative = true) {

  if (cha->isUndefined(name) && cha->hasFirstDefinedChild(name)) {
    name = cha->getFirstDefinedChild(name);
  }

  if (!newLayoutInds.count(name)) {
    sd_print("Vtbl %s %d, undefined: %d.\n",
        name.first.c_str(), name.second, cha->isUndefined(name));
    sd_print("has first child %d.\n", cha->hasFirstDefinedChild(name));
    if (cha->knowsAbout(name) && cha->hasFirstDefinedChild(name)) {
      sd_print("class: (%s, %lu) doesn't belong to newLayoutInds\n", name.first.c_str(), name.second);
      sd_print("%s has %u address points\n", name.first.c_str(), cha->getNumAddrPts(name.first));
      for (uint64_t i = 0; i < cha->getNumAddrPts(name.first); i++)
        sd_print("  addrPt: %d\n", cha->addrPt(name.first, i));
      assert(false);
    }

    return offset;
  }

  assert(cha->hasRange(name));
  std::vector<uint64_t>& newInds = newLayoutInds[name];
  const range_t& subVtableRange = cha->getRange(name);

  if (isRelative) {
    int64_t oldAddrPt = cha->addrPt(name) - subVtableRange.first;
    int64_t fullIndex = oldAddrPt + offset;

    if (! (fullIndex >= 0 && fullIndex <= (int64_t) (subVtableRange.second - subVtableRange.first))) {
      sd_print("error in translateVtblInd: %s, addrPt:%ld, old:%ld\n", name.first.c_str(), oldAddrPt, offset);
      assert(false);
    }

    return ((int64_t) newInds.at(fullIndex)) - ((int64_t) newInds.at(oldAddrPt));
  } else {
    assert(0 <= offset && offset <= (int64_t)newInds.size());
    return newInds[offset];
  }
}

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

  sd_print("Cleared SDLayoutBuilder analysis results\n");
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
 
  //collect GV and than remove
  for (auto itr = cha->oldVTables_begin(); itr != cha->oldVTables_end(); itr ++) {
    GlobalVariable* var = M.getGlobalVariable(itr->first, true);
    assert(var && var->use_empty());
//    sd_print("deleted vtbl: %s\n", var->getName().data());
    var->eraseFromParent();
  }

  // remove all original thunks from the module
  while (! vthunksToRemove.empty()) {
    Function* f = * vthunksToRemove.begin();
    vthunksToRemove.erase(f);
    f->eraseFromParent();
  }

  //collect and than remove
  for (Module::FunctionListType::iterator itr = M.getFunctionList().begin();
       itr != M.getFunctionList().end(); itr++){
    if (sd_isVthunk(itr->getName()) &&
        (itr->user_begin() == itr->user_end())) {
      vthunksToRemove.insert(itr);
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
  Constant* gvInt         = ConstantExpr::getPtrToInt(gv, IntPtrTy);
  Constant* offsetVal     = ConstantInt::get(IntPtrTy, addrPtOff * WORD_WIDTH);
  Constant* gvOffInt      = ConstantExpr::getAdd(gvInt, offsetVal);

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
  //first, we iterate through all roots contained in the cloud and/or order and interleave
  for (auto itr = cha->roots_begin(); itr != cha->roots_end(); itr++) {
   
    vtbl_name_t vtbl = *itr;         // get the v table name as string

    if (interleave)
      interleaveCloud(vtbl);         // interleave the cloud or
    else
      orderCloud(vtbl);              // order the cloud
      
    calculateNewLayoutInds(vtbl);    // calculate the new indices from the interleaved vtable
  }
  
  //second, we iterate through all roots contained in the cloud replace 
  //v thunks and emit global variables.
  for (auto itr = cha->roots_begin(); itr != cha->roots_end(); itr++) {
    vtbl_name_t vtbl = *itr;         // get the v table name as string
    createThunkFunctions(M, vtbl);   // replace the virtual thunks with the modified ones
    createNewVTable(M, vtbl);        // finally, emit the global variable
  }

  // third, we iterate through all roots contained in the cloud and 
  // calculate v pointer ranges and than verify the v pointer ranges
  for (auto itr = cha->roots_begin(); itr != cha->roots_end(); itr++) {
    vtbl_name_t vtbl = *itr;  // get the v table name as string
    calculateVPtrRanges(M, vtbl);
    verifyVPtrRanges(vtbl);
  }
}

