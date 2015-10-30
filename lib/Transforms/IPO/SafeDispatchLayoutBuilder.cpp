#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO/SafeDispatchLayoutBuilder.h"
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

#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>
#include <vector>
#include <set>
#include <map>
#include <algorithm>

using namespace llvm;

#define WORD_WIDTH 8
#define NEW_VTABLE_NAME(vtbl) ("_SD" + vtbl)
#define NEW_VTHUNK_NAME(fun,parent) ("_SVT" + parent + fun->getName().str())

char SDLayoutBuilder::ID = 0;

INITIALIZE_PASS_BEGIN(SDLayoutBuilder, "sdovt", "Oredered VTable Layout Builder for SafeDispatch", false, false)
INITIALIZE_PASS_DEPENDENCY(SDBuildCHA)
INITIALIZE_PASS_END(SDLayoutBuilder, "sdovt", "Oredered VTable Layout Builder for SafeDispatch", false, false)

bool SDLayoutBuilder::verifyNewLayouts(Module &M) {
  for (auto vtbl: cha->roots) { 
    vtbl_t root(vtbl, 0);
    assert(interleavingMap.count(vtbl) && cha->cloudMap.count(root));

    interleaving_list_t &interleaving = interleavingMap[vtbl];
    new_layout_inds_map_t indMap;
    uint64_t i = 0;

    // Build a map (vtbl_t -> (uint64_t -> uint64_t)) with the old-to-new index mapping encoded in the
    // interleaving
    for (auto elem : interleaving) {
      vtbl_t &v = elem.first;
      uint64_t oldPos = elem.second;

      if (indMap.find(v) == indMap.end()) {
        indMap[v] = std::map<uint64_t, uint64_t>();
      } else {
        if (indMap[v].count(oldPos) != 0) {
          std::cerr << "In ivtbl " << vtbl << " entry " << v.first << "," << v.second << "[" << oldPos << "]"
            << " appears twice - at " << indMap[v][oldPos] << " and " << i << std::endl;
          return false;
        }
      }

      indMap[v][oldPos] = i;
      i++;
    }

    SDBuildCHA::order_t cloud = cha->preorder(root);

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
          return false;
      }

      // Check that the index map is dense (total on the range of indices)
      int64_t oldVtblSize = cha->rangeMap[n.first][n.second].second - \
                            cha->rangeMap[n.first][n.second].first + 1;
      auto minMax = std::minmax_element (indMap[n].begin(), indMap[n].end());

      if ((minMax.second->first - minMax.first->first + 1) != oldVtblSize) {
          std::cerr << "In ivtbl " << vtbl << " min-max rangefor "
            << n.first << "," << n.second << 
            " is (" << minMax.first->first << "-"
            << minMax.second->first << ") expected size "
            << oldVtblSize << std::endl;
          return false;
      }

      if (indMap[n].size() != oldVtblSize) {
          std::cerr << "In ivtbl " << vtbl << " index mapping for " << n.first << "," << n.second << 
            " has " << indMap[n].size() << " expected " << oldVtblSize << std::endl;
          return false;
      }
    }

/*
  // 2) Check that the relative vtable offsets are the same for every parent/child class pair
  for (const vtbl_t& pt : cloud) {
    if (isUndefined(pt.first))  continue;

    for(const vtbl_t& child : cloudMap[pt]) {
      if (isUndefined(child.first))  continue;

      uint64_t ptStart = rangeMap[pt.first][pt.second].first;
      uint64_t ptEnd = rangeMap[pt.first][pt.second].second;
      uint64_t ptAddrPt = addrPtMap[pt.first][pt.second];
      uint64_t ptRelAddrPt = ptAddrPt - ptStart;
      uint64_t childStart = rangeMap[child.first][child.second].first;
      uint64_t childEnd = rangeMap[child.first][child.second].second;
      uint64_t childAddrPt = addrPtMap[child.first][child.second];
      uint64_t childRelAddrPt = childAddrPt - childStart;

      int64_t ptToChildAdj = childAddrPt - ptAddrPt;

      uint64_t newPtAddrPt = indMap[pt][ptAddrPt];
      uint64_t newChildAddrPt = indMap[child][childAddrPt];

      if (ptToChildAdj < 0 || ptEnd + ptToChildAdj > childEnd) {
        sd_print("Parent vtable(%s,%d) [%d,%d,%d] is not contained in child vtable(%s,%d) [%d,%d,%d]",
            pt.first.c_str(), pt.second, ptStart, ptAddrPt, ptEnd,
            child.first.c_str(), child.second, childStart, childAddrPt, childEnd);
        return false;
      }

      for (int64_t ind = 0; ind < ptEnd - ptStart + 1; ind++) {
        int64_t newPtInd = indMap[pt][ind] - newPtAddrPt;
        int64_t newChildInd = indMap[child][ind + ptToChildAdj] - newChildAddrPt;
        if (newPtInd != newChildInd) {
          sd_print("Parent (%s,%d) old relative index %d(new relative %d) mismatches child(%s,%d) corresponding old index %d(new relative %d)",
              pt.first.c_str(), pt.second, (ind - ptAddrPt), newPtInd,
              child.first.c_str(), child.second, ind + ptToChildAdj - childAddrPt, newChildInd);
          return false;
        }
      }
    }
  }
  */
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
    if (cha->oldVTables.count(vtbl) == 0) {
      assert(cha->undefinedVTables.count(vtbl));
      continue;
    }
    ConstantArray* vtableArr = cha->oldVTables[vtbl];

    // iterate over the vtable elements
    for (unsigned vtblInd = 0; vtblInd < vtableArr->getNumOperands(); ++vtblInd) {
      Constant* c = vtableArr->getOperand(vtblInd);
      Function* thunkF = getVthunkFunction(c);
      if (! thunkF)
        continue;

      // find the index of the sub-vtable inside the whole
      unsigned order = cha->getVTableOrder(vtbl, vtblInd);

      // this should have a parent
      assert(cha->subObjNameMap.count(vtbl) && cha->subObjNameMap[vtbl].size() > order);
      std::string& parentClass = cha->subObjNameMap[vtbl][order];

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
            sd_print("Create thunk function %s\n", newThunkName.c_str());
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

void SDLayoutBuilder::orderCloud(SDLayoutBuilder::vtbl_name_t& vtbl) {
  sd_print("Ordering...\n");
  assert(cha->roots.count(vtbl));

  // create a temporary list for the positive part
  interleaving_vec_t orderedVtbl;

  vtbl_t root(vtbl,0);
  order_t pre = cha->preorder(root);
  uint64_t max = 0;

  for(const vtbl_t child : pre) {
    range_t r = cha->rangeMap[child.first][child.second];
    uint64_t size = r.second - r.first + 1;
    if (size > max)
      max = size;

    // record which cloud the current sub-vtable belong to
    if (cha->ancestorMap.find(child) == cha->ancestorMap.end()) {
      cha->ancestorMap[child] = vtbl;
    }
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
    if(cha->undefinedVTables.count(child.first))
      continue;

    range_t r = cha->rangeMap[child.first][child.second];
    uint64_t size = r.second - r.first + 1;
    uint64_t addrpt = cha->addrPtMap[child.first][child.second] - r.first;
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
}

void SDLayoutBuilder::interleaveCloud(SDLayoutBuilder::vtbl_name_t& vtbl) {
  sd_print("Interleaving...\n");
  assert(cha->roots.count(vtbl));

  // create a temporary list for the positive part
  interleaving_list_t positivePart;

  vtbl_t root(vtbl,0);
  order_t pre = cha->preorder(root);

  // initialize the cloud's interleaving list
  interleavingMap[vtbl] = interleaving_list_t();

  // fill both parts
  fillVtablePart(interleavingMap[vtbl], pre, false);
  fillVtablePart(positivePart, pre, true);

  // append positive part to the negative
  interleavingMap[vtbl].insert(interleavingMap[vtbl].end(), positivePart.begin(), positivePart.end());
  alignmentMap[vtbl] = WORD_WIDTH;

  for (auto child:  pre) {
    if (cha->ancestorMap.find(child) == cha->ancestorMap.end()) {
      cha->ancestorMap[child] = vtbl;
    }
  }
}

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
    if (cha->undefinedVTables.find(ivtbl.first.first) != cha->undefinedVTables.end() ||
        ivtbl.first == dummyVtable) {
      newVtableElems.push_back(Constant::getNullValue(IntegerType::getInt8PtrTy(C)));
    } else {
      assert(cha->oldVTables.find(ivtbl.first.first) != cha->oldVTables.end());

      ConstantArray* vtable = cha->oldVTables[ivtbl.first.first];
      Constant* c = vtable->getOperand(ivtbl.second);
      Function* thunk = getVthunkFunction(c);

      if (thunk) {
        Function* newThunk = M.getFunction(
              NEW_VTHUNK_NAME(thunk, cha->subObjNameMap[ivtbl.first.first][ivtbl.first.second]));
        assert(newThunk);

        Constant* newC = ConstantExpr::getBitCast(newThunk, IntegerType::getInt8PtrTy(C));
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
  GlobalVariable* newVtable = new GlobalVariable(M, newArrType, true,
                                                 GlobalVariable::InternalLinkage,
                                                 nullptr, NEW_VTABLE_NAME(vtbl));
  assert(alignmentMap.count(vtbl));
  newVtable->setAlignment(alignmentMap[vtbl]);
  newVtable->setInitializer(newVtableInit);
  newVtable->setUnnamedAddr(true);

  cloudStartMap[NEW_VTABLE_NAME(vtbl)] = newVtable;

  // to start changing the original uses of the vtables, first get all the classes in the cloud
  order_t cloud;
  vtbl_t root(vtbl,0);
  cha->preorderHelper(cloud, root);

  Constant* zero = ConstantInt::get(M.getContext(), APInt(64, 0));
  for (const vtbl_t& v : cloud) {
    if (cha->isDefined(v)) {
      assert(newVTableStartAddrMap.find(v) == newVTableStartAddrMap.end());
      newVTableStartAddrMap[v] = newVtblAddressConst(M, v);
    }

    if (cha->undefinedVTables.find(v.first) != cha->undefinedVTables.end())
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
      uint64_t order = 0;
      std::vector<uint64_t>& addrPts = cha->addrPtMap[v.first];
      for(; order < addrPts.size() && addrPts[order] != oldAddrPt; order++);
      assert(order != addrPts.size());

      // if this is not referring to the current part, continue
      if (order != v.second)
        continue;

      // find the offset relative to the sub-vtable start
      int addrInsideBlock = oldAddrPt - cha->rangeMap[v.first][order].first;

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

static bool sd_isLE(int64_t lhs, int64_t rhs) { return lhs <= rhs; }
static bool sd_isGE(int64_t lhs, int64_t rhs) { return lhs >= rhs; }

void SDLayoutBuilder::fillVtablePart(SDLayoutBuilder::interleaving_list_t& vtblPart, const SDLayoutBuilder::order_t& order, bool positiveOff) {
  std::map<vtbl_t, int64_t> posMap;     // current position
  std::map<vtbl_t, int64_t> lastPosMap; // last possible position

  for(const vtbl_t& n : order) {
    uint64_t addrPt = cha->addrPtMap[n.first][n.second];  // get the address point of the vtable
    posMap[n]     = positiveOff ? addrPt : (addrPt - 1); // start interleaving from that address
    lastPosMap[n] = positiveOff ? (cha->rangeMap[n.first][n.second].second) :
                                  (cha->rangeMap[n.first][n.second].first);
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

int64_t SDLayoutBuilder::translateVtblInd(SDLayoutBuilder::vtbl_t name, int64_t offset,
                                bool isRelative = true) {

  if (cha->isUndefined(name) && cha->hasFirstDefinedChild(name)) {
    name = cha->getFirstDefinedChild(name);
  }

  if (!newLayoutInds.count(name)) {
    sd_print("Vtbl %s %d, undefined: %d.\n",
        name.first.c_str(), name.second, cha->isUndefined(name));
    sd_print("has first child %d.\n", cha->hasFirstDefinedChild(name));
    if (cha->knowsAbout(name) && cha->hasFirstDefinedChild(name)) {
      sd_print("class: (%s, %lu) doesn't belong to newLayoutInds\n", name.first.c_str(), name.second);
      sd_print("%s has %u address points\n", name.first.c_str(), cha->addrPtMap[name.first].size());
      for (int i = 0; i < cha->addrPtMap[name.first].size(); i++)
        sd_print("  addrPt: %d\n", cha->addrPtMap[name.first][i]);
      assert(false);
    }

    return offset;
  }

  assert(cha->rangeMap.find(name.first) != cha->rangeMap.end() &&
         cha->rangeMap[name.first].size() > name.second);

  std::vector<uint64_t>& newInds = newLayoutInds[name];
  range_t& subVtableRange = cha->rangeMap[name.first].at(name.second);

  if (isRelative) {
    int64_t oldAddrPt = cha->addrPtMap[name.first].at(name.second) - subVtableRange.first;
    int64_t fullIndex = oldAddrPt + offset;

    if (! (fullIndex >= 0 && fullIndex <= ((int64_t) subVtableRange.second - subVtableRange.first))) {
      sd_print("error in translateVtblInd: %s, addrPt:%ld, old:%ld\n", name.first.c_str(), oldAddrPt, offset);
      assert(false);
    }

    return ((int64_t) newInds.at(fullIndex)) - ((int64_t) newInds.at(oldAddrPt));
  } else {
    assert(0 <= offset && offset <= newInds.size());
    return newInds[offset];
  }
}

  /*
int64_t SDLayoutBuilder::oldIndexToNew(SDLayoutBuilder::vtbl_name_t vtbl, int64_t offset,
                                bool isRelative = true) {
  return offset;
  vtbl_t name(vtbl,0);

  sd_print("class : %s offset: %d\n", name.first.data(), offset);
  if (cha->isUndefined(name)) {
    name = cha->getFirstDefinedChild(name);
  }

  // if the class doesn't have any vtable defined,
  // use one of its children to calculate function ptr offset
  if (newLayoutInds.find(name) == newLayoutInds.end()) {
    // i don't know if works for negative offsets too
    assert(isRelative);
    if(offset < 0) {
      sd_print("offset: %ld\n", offset);
      assert(false);
    }

    // this is a class we don't have any metadata about (i.e. there is no child of its
    // that has a defined vtable). We assume this should never get called in a
    // statically linked binary.
    assert(!cha->knowsAbout(name));
    return offset;
  }

  return translateVtblInd(name, offset, isRelative);
}
  */

llvm::Constant* SDLayoutBuilder::getVTableRangeStart(const SDLayoutBuilder::vtbl_t& vtbl) {
  return newVTableStartAddrMap[vtbl];
}

void SDLayoutBuilder::clearAnalysisResults() {
  cha->clearAnalysisResults();
  newLayoutInds.clear();
  interleavingMap.clear();

  sd_print("Cleared SDLayoutBuilder analysis results\n");
}

/// ----------------------------------------------------------------------------
/// SDChangeIndices implementation
/// ----------------------------------------------------------------------------

void SDLayoutBuilder::removeOldLayouts(Module &M) {
  for (auto itr : cha->oldVTables) {
    GlobalVariable* var = M.getGlobalVariable(itr.first, true);
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

  assert(cha->ancestorMap.count(vtbl));
  vtbl_name_t rootName = cha->ancestorMap[vtbl];

  // sanity checks
  assert(cha->roots.count(rootName));

  // switch to the new vtable name
  rootName = NEW_VTABLE_NAME(rootName);

  // we should add the address point of the given class
  // inside the new interleaved vtable to the start address
  // of the vtable

  // find which element is the address point
  assert(cha->addrPtMap.count(name));
  unsigned addrPt = cha->addrPtMap[name][0];

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

Constant* SDLayoutBuilder::newVtblAddressConst(Module& M, const vtbl_t& vtbl) {
  const DataLayout &DL = M.getDataLayout();
  assert(cha->ancestorMap.count(vtbl));
  vtbl_name_t rootName = cha->ancestorMap[vtbl];
  LLVMContext& C = M.getContext();
  Type *IntPtrTy = DL.getIntPtrType(C);

  // sanity checks
  assert(cha->roots.count(rootName));

  // switch to the new vtable name
  rootName = NEW_VTABLE_NAME(rootName);

  // we should add the address point of the given class
  // inside the new interleaved vtable to the start address
  // of the vtable

  // find which element is the address point
  assert(cha->addrPtMap.count(vtbl.first));
  unsigned addrPt = cha->addrPtMap[vtbl.first][vtbl.second] - cha->rangeMap[vtbl.first][vtbl.second].first;

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

/**
 * Interleave the generated clouds and create a new global variable for each of them.
 */
void SDLayoutBuilder::buildNewLayouts(Module &M) {
  for (roots_t::iterator itr = cha->roots.begin(); itr != cha->roots.end(); itr++) {
    vtbl_name_t vtbl = *itr;

    if (interleave)
      interleaveCloud(vtbl);         // order the cloud
    else
      orderCloud(vtbl);         // order the cloud
    calculateNewLayoutInds(vtbl);  // calculate the new indices from the interleaved vtable
  }

  for (roots_t::iterator itr = cha->roots.begin(); itr != cha->roots.end(); itr++) {
    vtbl_name_t vtbl = *itr;
    createThunkFunctions(M, vtbl); // replace the virtual thunks with the modified ones
    createNewVTable(M, vtbl);      // finally, emit the global variable

    // exploit this loop to calculate the sizes of all possible subgraphs
    // that has a primary vtable as a root
    cha->calculateChildrenCounts(vtbl_t(vtbl,0));
  }
}

