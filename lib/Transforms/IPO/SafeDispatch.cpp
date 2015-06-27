#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"
#include "llvm/Transforms/IPO/SafeDispatchCheck.h"

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>
#include <vector>
#include <set>
#include <map>
#include <algorithm>

// you have to modify the following files for each additional LLVM pass
// 1. IPO.h and IPO.cpp
// 2. LinkAllPasses.h
// 3. InitializePasses.h

using namespace llvm;

#define DEBUG_TYPE "cc"
#define WORD_WIDTH 8

#define NEW_VTABLE_NAME(vtbl) ("_SD" + vtbl)
#define NEW_VTHUNK_NAME(fun,parent) ("_SVT" + parent + fun->getName().str())

static llvm::Module* CURR_MODULE = NULL;

namespace {


  /**
   * Module pass for the SafeDispatch Gold Plugin
   */
  class SDModule : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid

    SDModule() : ModulePass(ID) {
      initializeSDModulePass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDModule() {
      sd_print("deleting SDModule pass\n");
    }

    // variable definitions

    typedef std::string                                     vtbl_name_t;
    typedef std::pair<vtbl_name_t, uint64_t>                vtbl_t;
    typedef std::set<vtbl_t>                                cloud_map_children_t;
    typedef std::map<vtbl_t, cloud_map_children_t>          cloud_map_t;
    typedef std::set<vtbl_name_t>                           roots_t;
    typedef std::map<vtbl_name_t, std::vector<uint64_t>>    addrpt_map_t;
    typedef std::pair<uint64_t, uint64_t>                   range_t;
    typedef std::map<vtbl_name_t, std::vector<range_t>>     range_map_t;
    typedef std::map<vtbl_t, vtbl_name_t>                   ancestor_map_t;
    typedef std::map<vtbl_t, std::vector<uint64_t>>         new_layout_inds_t;
    typedef std::pair<vtbl_t, uint64_t>       					    interleaving_t;
    typedef std::list<interleaving_t>                       interleaving_list_t;
    typedef std::map<vtbl_name_t, interleaving_list_t>      interleaving_map_t;
    typedef std::vector<vtbl_t>                             order_t;
    typedef std::map<vtbl_name_t, std::vector<vtbl_name_t>> subvtbl_map_t;
    typedef std::map<vtbl_name_t, ConstantArray*>           oldvtbl_map_t;

    cloud_map_t cloudMap;                              // (vtbl,ind) -> set<(vtbl,ind)>
    roots_t roots;                                     // set<vtbl>
    subvtbl_map_t subObjNameMap;                       // vtbl -> [vtbl]
    addrpt_map_t addrPtMap;                            // vtbl -> [addr pt]
    range_map_t rangeMap;                              // vtbl -> [(start,end)]
    ancestor_map_t ancestorMap;                        // (vtbl,ind) -> root vtbl
    new_layout_inds_t newLayoutInds;                   // (vtbl,ind) -> [new ind inside interleaved vtbl]
    interleaving_map_t interleavingMap;                // root -> new layouts map
    oldvtbl_map_t oldVTables;                          // vtbl -> &[vtable element]
    std::map<vtbl_name_t, uint32_t> cloudSizeMap;      // vtbl -> # vtables derived from (vtbl,0)
    std::set<vtbl_name_t> undefinedVTables;            // contains dynamic classes that don't have vtables defined

    // these should match the structs defined at SafeDispatchVtblMD.h
    struct nmd_sub_t {
      uint64_t order;
      vtbl_name_t parentName;
      uint64_t start; // range boundaries are inclusive
      uint64_t end;
      uint64_t addressPoint;
    };

    struct nmd_t {
      vtbl_name_t className;
      std::vector<nmd_sub_t> subVTables;
    };

    /**
     * 1. a. Iterate NamedMDNodes to build CHA forest F.
     *       => map<pair<vtbl,ind>, vector<pair<vtbl,ind>>>
     *    b. Take note of the roots of the forest.
     *       => set<vtbl>
     *    c. Keep the original address point map
     *       => map<vtbl, vector<int>>
     *    d. Keep the original sub-vtable ranges
     *       => map<vtbl, vector<int>>
     *    e. Calculate which sub-vtable belongs to which cloud.
     *       => map<pair<vtbl,ind>, vtbl>
     *
     * 2. For each cloud:
     *    a. Interleave the clouds
     *    b. Calculate the new layout indices map.
     *       => map<pair<vtbl,ind>, vector<int>>
     *    c. Create a GlobalVariable for each cloud
     */
    bool runOnModule(Module &M) {
      CURR_MODULE = &M;
      sd_print("Started safedispatch analysis\n");

      vcallMDId = M.getMDKindID(SD_MD_VCALL);

      buildClouds(M);          // part 1
      printClouds();
      interleaveClouds(M);     // part 2

      sd_print("Finished safedispatch analysis\n");

      return roots.size() > 0;
    }

    void clearAnalysisResults();

    /**
     * Calculates the vtable order number given the index relative to
     * the beginning of the vtable
     */
    unsigned getVTableOrder(const vtbl_name_t& vtbl, uint64_t ind) {
      assert(addrPtMap.find(vtbl) != addrPtMap.end());

      std::vector<uint64_t>& arr = addrPtMap[vtbl];
      std::vector<uint64_t>::iterator itr = std::upper_bound(arr.begin(), arr.end(), ind);
      return (itr - arr.begin() - 1);
    }

    /**
     * New starting address point inside the interleaved vtable
     */
    uint64_t newVtblAddressPoint(const vtbl_name_t& name) {
      vtbl_t vtbl(name,0);
      assert(newLayoutInds.count(vtbl));
      return newLayoutInds[vtbl][0];
    }

    /**
     * Get the vtable address of the class in the interleaved scheme,
     * and insert the necessary instructions before the given instruction
     */
    Value* newVtblAddress(Module& M, const vtbl_name_t& name, Instruction* inst) {
      vtbl_t vtbl(name,0);

      assert(ancestorMap.count(vtbl));
      vtbl_name_t rootName = ancestorMap[vtbl];

      // sanity checks
      assert(roots.count(rootName));

      // switch to the new vtable name
      rootName = NEW_VTABLE_NAME(rootName);

      // we should add the address point of the given class
      // inside the new interleaved vtable to the start address
      // of the vtable

      // find which element is the address point
      assert(addrPtMap.count(name));
      unsigned addrPt = addrPtMap[name][0];

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

    void removeVtablesAndThunks(Module &M);

  private:

    /**
     * Reads the NamedMDNodes in the given module and creates the class hierarchy
     */
    void buildClouds(Module &M);

    void printClouds();

    /**
     * Interleave the generated clouds and create a new global variable for each of them.
     */
    void interleaveClouds(Module &M) {
      for (roots_t::iterator itr = roots.begin(); itr != roots.end(); itr++) {
        vtbl_name_t vtbl = *itr;

        interleaveCloud(vtbl);         // interleave the cloud
        calculateNewLayoutInds(vtbl);  // calculate the new indices from the interleaved vtable
      }

      for (roots_t::iterator itr = roots.begin(); itr != roots.end(); itr++) {
        vtbl_name_t vtbl = *itr;
        createThunkFunctions(M, vtbl); // replace the virtual thunks with the modified ones
        createNewVTable(M, vtbl);      // finally, emit the global variable

        // exploit this loop to calculate the sizes of all possible subgraphs
        // that has a primary vtable as a root
        calculateChildrenCounts(vtbl_t(vtbl,0));
      }
    }

    /**
     * Extract the vtable info from the metadata and put it into a struct
     */
    std::vector<nmd_t> extractMetadata(NamedMDNode* md);

    /**
     * Interleave the cloud given by the root element.
     */
    void interleaveCloud(vtbl_name_t& vtbl);

    /**
     * Calculate the new layout indices for each vtable inside the given cloud
     */
    void calculateNewLayoutInds(vtbl_name_t& vtbl);

    /**
     * Interleave the actual vtable elements inside the cloud and
     * create a new global variable
     */
    void createNewVTable(Module& M, vtbl_name_t& vtbl);

    /**
     * This method is used for filling the both (negative and positive) parts of an
     * interleaved vtable of a cloud.
     *
     * @param part        : A list reference to record the <vtbl_t, element index> pairs
     * @param order       : A list that contains the preorder traversal
     * @param positiveOff : true if we're filling the positive (function pointers) part
     */
    void fillVtablePart(interleaving_list_t& part, const order_t& order, bool positiveOff);

    /**
     * Recursive function that calculates the number of deriving sub-vtables of each
     * primary vtable
     */
    uint32_t calculateChildrenCounts(const vtbl_t& vtbl);

    void handleUndefinedVtables(std::set<vtbl_name_t>& undefVtbls);

    /**
     * These functions and variables used to deal with duplication
     * of the vthunks in the vtables
     */
    unsigned vcallMDId;
    std::set<Function*> vthunksToRemove;

    void createThunkFunctions(Module&, const vtbl_name_t& rootName);
    void updateVcallOffset(Instruction *inst, const vtbl_name_t& className, unsigned order);
    Function* getVthunkFunction(Constant* vtblElement);

  public:

    /**
     * Return a list that contains the preorder traversal of the tree
     * starting from the given node
     */
    order_t preorder(vtbl_t& root);
    void preorderHelper(order_t& nodes, const vtbl_t& root);

    /**
     * Converts an index in the original primary vtable into the new one
     * If isRelative is true, index is assumed to be relative to the
     * address point, otherwise it'll be relative to the start of the vtbl.
     *
     * First one get vtable name and calls the second one with the
     * first parameter (vtbl_name,0)
     */
    int64_t oldIndexToNew(vtbl_name_t vtbl_name, int64_t offset, bool isRelative);
    int64_t oldIndexToNew2(vtbl_t vtbl, int64_t offset, bool isRelative);
  };

  /**
   * Pass for updating the annotated instructions with the new indices
   */
  struct SDChangeIndices : public ModulePass {
    static char ID; // Pass identification, replacement for typeid

    SDChangeIndices() : ModulePass(ID) {
      initializeSDChangeIndicesPass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDChangeIndices() {
      sd_print("deleting SDChangeIndices pass\n");
    }

    bool runOnModule(Module &M) override {
      CURR_MODULE = &M;
      sdModule = &getAnalysis<SDModule>();
      assert(sdModule);

      sd_print("inside the 2nd pass\n");

      classNameMDId = M.getMDKindID(SD_MD_VFUN_CALL);
      castFromMDId  = M.getMDKindID(SD_MD_CAST_FROM);
      typeidMDId    = M.getMDKindID(SD_MD_TYPEID);
      vcallMDId     = M.getMDKindID(SD_MD_VCALL);
      vbaseMDId     = M.getMDKindID(SD_MD_VBASE);
      memptrMDId    = M.getMDKindID(SD_MD_MEMPTR);
      memptr2MDId   = M.getMDKindID(SD_MD_MEMPTR2);
      memptrOptMdId = M.getMDKindID(SD_MD_MEMPTR_OPT);
      checkMdId     = M.getMDKindID(SD_MD_CHECK);

      uint64_t noOfFunctions = M.getFunctionList().size();
      uint64_t currFunctionInd = 1;
      int pct, lastpct=0;

      for(Module::iterator f_itr =M.begin(); f_itr != M.end(); f_itr++) {
        pct = ((double) currFunctionInd / noOfFunctions) * 100;
        currFunctionInd++;

        if (pct > lastpct) {
          fprintf(stderr, "\rSD] Progress: %3d%%", pct);
          fflush(stderr);
          lastpct = pct;
        }

        for(Function:: iterator bb_itr = f_itr->begin(); bb_itr != f_itr->end(); bb_itr++) {
          updateBasicBlock2(&M, *bb_itr);
        }
      }
      fprintf(stderr, "\n");
      sd_print("Finished running the 2nd pass...\n");

      sdModule->removeVtablesAndThunks(M);

      sdModule->clearAnalysisResults();

      sd_print("removed thunks...\n");

      return true;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<SDModule>();
    }

  private:

    SDModule* sdModule;

    /// these are used to make sure that an instruction is modified only
    /// at one place in the program
    std::set<Instruction*>* changedInstructions;
    void sanityCheck1(Instruction* inst) {
      assert(changedInstructions->find(inst) == changedInstructions->end());
      changedInstructions->insert(inst);
    }

    // metadata ids
    unsigned classNameMDId;
    unsigned castFromMDId;
    unsigned typeidMDId;
    unsigned vcallMDId;
    unsigned vbaseMDId;
    unsigned memptrMDId;
    unsigned memptr2MDId;
    unsigned memptrOptMdId;
    unsigned checkMdId;

    /**
     * Change the instructions inside the given basic block
     */
    bool updateBasicBlock(Module* M, BasicBlock& BB);
    void updateBasicBlock2(Module* M, BasicBlock& BB);

    /**
     * Update the function pointer index inside the GEP instruction
     */
    void updateVfptrIndex(llvm::MDNode* mdNode, Instruction* inst);

    /**
     * Redirect the call to the __dynamic_cast to __ivtbl_dynamic_cast
     */
    void replaceDynamicCast(Module* module, Instruction* inst, llvm::MDNode* mdNode);

    /**
     * Change the RTTI offset inside the GEP of load instruction
     */
    void updateRTTIOffset(Instruction* inst, llvm::MDNode* mdNode);

    /**
     * Replace the constant struct that holds the virtual member pointer
     * inside the instruction
     */
    void replaceConstantStruct(ConstantStruct* CS, Instruction* inst, std::string& className);

    /**
     * Change the constant struct that holds the virtual member pointer
     * inside the store instruction
     */
    void handleStoreMemberPointer(llvm::MDNode* mdNode, Instruction* inst);

    /**
     * Since member pointers are implemented as a constant, they can be used
     * inside a select instruction. Handle this special case separately.
     */
    void handleSelectMemberPointer(llvm::MDNode* mdNode, Instruction* inst);

    /**
     * Change the check range and start constant before the vfun call
     */
    void handleVtableCheck_1(Module* M, llvm::MDNode* mdNode, Instruction* inst);
    void handleVtableCheck_2(Module* M, llvm::MDNode* mdNode, Instruction* inst);

    void handleVtableCheck(Module* M, llvm::MDNode* mdNode, Instruction* inst) {
      switch(SD_CHECK_TYPE){
      case SD_CHECK_1:
        handleVtableCheck_1(M,mdNode,inst);
        break;
      case SD_CHECK_2:
        handleVtableCheck_2(M,mdNode,inst);
        break;
      }
    }

    /**
     * Extract the constant from the given MDTuple at the given operand
     */
    int64_t getMetadataConstant(llvm::MDNode* mdNode, unsigned operandNo);

    /**
     * Create the function type of the new dynamic cast function
     */
    FunctionType* getDynCastFunType(LLVMContext& context);
  };
}

char SDChangeIndices::ID = 0;
char SDModule::ID = 0;

INITIALIZE_PASS(SDModule, "sdmp", "Module pass for SafeDispatch", false, false)

INITIALIZE_PASS_BEGIN(SDChangeIndices, "cc", "Change Constant", false, false)
INITIALIZE_PASS_DEPENDENCY(SDModule)
INITIALIZE_PASS_END(SDChangeIndices, "cc", "Change Constant", false, false)


ModulePass* llvm::createSDChangeIndicesPass() {
  return new SDChangeIndices();
}

ModulePass* llvm::createSDModulePass() {
  return new SDModule();
}

/// ----------------------------------------------------------------------------
/// Analysis implementation
/// ----------------------------------------------------------------------------
Function* SDModule::getVthunkFunction(Constant* vtblElement) {
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

void SDModule::updateVcallOffset(Instruction *inst, const vtbl_name_t& className, unsigned order) {
  BitCastInst* bcInst = dyn_cast<BitCastInst>(inst);
  assert(bcInst);
  GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(bcInst->getOperand(0));
  assert(gepInst);
  LoadInst* loadInst = dyn_cast<LoadInst>(gepInst->getOperand(1));
  assert(loadInst);
  BitCastInst* bcInst2 = dyn_cast<BitCastInst>(loadInst->getOperand(0));
  assert(bcInst2);
  GetElementPtrInst* gepInst2 = dyn_cast<GetElementPtrInst>(bcInst2->getOperand(0));
  assert(gepInst2);

  unsigned vcallOffsetOperandNo = 1;

  ConstantInt* oldVal = dyn_cast<ConstantInt>(gepInst2->getOperand(vcallOffsetOperandNo));
  assert(oldVal);

  // extract the old index
  int64_t oldIndex = oldVal->getSExtValue() / WORD_WIDTH;

  // calculate the new one
  int64_t newIndex = oldIndexToNew2(vtbl_t(className,order), oldIndex, true);

  // update the value
  sd_changeGEPIndex(gepInst2, vcallOffsetOperandNo, newIndex * WORD_WIDTH);

  // remove the metadata, doesn't mess with in the 2nd pass
  inst->setMetadata(vcallMDId, NULL);
}

void SDModule::createThunkFunctions(Module& M, const vtbl_name_t& rootName) {
  // for all defined vtables
  vtbl_t root(rootName,0);
  order_t vtbls = preorder(root);

  for (unsigned i=0; i < vtbls.size(); i++) {
    const vtbl_name_t& vtbl = vtbls[i].first;
    if (oldVTables.count(vtbl) == 0) {
      assert(undefinedVTables.count(vtbl));
      continue;
    }
    ConstantArray* vtableArr = oldVTables[vtbl];

    // iterate over the vtable elements
    for (unsigned vtblInd = 0; vtblInd < vtableArr->getNumOperands(); ++vtblInd) {
      Constant* c = vtableArr->getOperand(vtblInd);
      Function* thunkF = getVthunkFunction(c);
      if (! thunkF)
        continue;

      // find the index of the sub-vtable inside the whole
      unsigned order = getVTableOrder(vtbl, vtblInd);

      // this should have a parent
      assert(subObjNameMap.count(vtbl) && subObjNameMap[vtbl].size() > order);
      std::string& parentClass = subObjNameMap[vtbl][order];

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

      bool foundMD = false;

      // go over its instructions and replace the one with the metadata
      for(Function:: iterator bb_itr = newThunkF->begin(); bb_itr != newThunkF->end(); bb_itr++) {
        for(BasicBlock:: iterator i_itr = bb_itr->begin(); i_itr != bb_itr->end(); i_itr++) {
          Instruction* inst = i_itr;
          if ((inst->getMetadata(vcallMDId))) {
            updateVcallOffset(inst, vtbl, order);
            foundMD = true;
          }
        }
      }

      // this function should have a metadata
      assert(foundMD);
    }
  }
}

void SDModule::interleaveCloud(SDModule::vtbl_name_t& vtbl) {
  assert(roots.count(vtbl));

  // create a temporary list for the positive part
  interleaving_list_t positivePart;

  vtbl_t root(vtbl,0);
  order_t pre = preorder(root);

  // initialize the cloud's interleaving list
  interleavingMap[vtbl] = interleaving_list_t();

  // fill both parts
  fillVtablePart(interleavingMap[vtbl], pre, false);
  fillVtablePart(positivePart, pre, true);

  // append positive part to the negative
  interleavingMap[vtbl].insert(interleavingMap[vtbl].end(), positivePart.begin(), positivePart.end());
}

void SDModule::calculateNewLayoutInds(SDModule::vtbl_name_t& vtbl){
  assert(interleavingMap.count(vtbl));

  uint64_t currentIndex = 0;
  for (const interleaving_t& ivtbl : interleavingMap[vtbl]) {
    // record which cloud the current sub-vtable belong to
    if (ancestorMap.find(ivtbl.first) == ancestorMap.end()) {
      ancestorMap[ivtbl.first] = vtbl;
    }

    // record the new index of the vtable element coming from the current vtable
    newLayoutInds[ivtbl.first].push_back(currentIndex++);
  }
}

void SDModule::createNewVTable(Module& M, SDModule::vtbl_name_t& vtbl){
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
    if (undefinedVTables.find(ivtbl.first.first) != undefinedVTables.end()) {
      newVtableElems.push_back(Constant::getNullValue(IntegerType::getInt8PtrTy(C)));
    } else {
      assert(oldVTables.find(ivtbl.first.first) != oldVTables.end());

      ConstantArray* vtable = oldVTables[ivtbl.first.first];
      Constant* c = vtable->getOperand(ivtbl.second);
      Function* thunk = getVthunkFunction(c);

      if (thunk) {
        Function* newThunk = M.getFunction(
              NEW_VTHUNK_NAME(thunk, subObjNameMap[ivtbl.first.first][ivtbl.first.second]));
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
                                                 GlobalVariable::ExternalLinkage,
                                                 nullptr, NEW_VTABLE_NAME(vtbl));
  newVtable->setAlignment(WORD_WIDTH);
  newVtable->setInitializer(newVtableInit);

  // to start changing the original uses of the vtables, first get all the classes in the cloud
  order_t cloud;
  vtbl_t root(vtbl,0);
  preorderHelper(cloud, root);

  Constant* zero = ConstantInt::get(M.getContext(), APInt(64, 0));
  for (const vtbl_t& v : cloud) {
    if (undefinedVTables.find(v.first) != undefinedVTables.end())
      continue;

    // find the original vtable
    GlobalVariable* globalVar = M.getGlobalVariable(v.first, true);
    assert(globalVar);

    // since we change the collection while we're iterating it,
    // put the users into a separate set first
    std::set<User*> users(globalVar->user_begin(), globalVar->user_end());

    // replace the uses of the original vtables
    for (std::set<User*>::iterator userItr = users.begin(); userItr != users.end(); userItr++) {
      // this should be a getelementptr
      User* user = *userItr;
      assert(user);

      ConstantExpr* userCE = dyn_cast<ConstantExpr>(user);

      // you are here
      assert(userCE && userCE->getOpcode() == GEP_OPCODE);

      // get the address pointer from the instruction
      ConstantInt* oldConst = dyn_cast<ConstantInt>(userCE->getOperand(2));
      assert(oldConst);
      uint64_t oldAddrPt = oldConst->getSExtValue();

      // find which part of the vtable the constructor uses
      uint64_t order = 0;
      std::vector<uint64_t>& addrPts = addrPtMap[v.first];
      for(; order < addrPts.size() && addrPts[order] != oldAddrPt; order++);
      assert(order != addrPts.size());

      // if this is not referring to the current part, continue
      if (order != v.second)
        continue;

      // find the offset relative to the sub-vtable start
      int addrInsideBlock = oldAddrPt - rangeMap[v.first][order].first;

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

void SDModule::fillVtablePart(SDModule::interleaving_list_t& vtblPart, const SDModule::order_t& order, bool positiveOff) {
  std::map<vtbl_t, int64_t> posMap;     // current position
  std::map<vtbl_t, int64_t> lastPosMap; // last possible position

  for(const vtbl_t& n : order) {
    uint64_t addrPt = addrPtMap[n.first][n.second];  // get the address point of the vtable
    posMap[n]     = positiveOff ? addrPt : (addrPt - 1); // start interleaving from that address
    lastPosMap[n] = positiveOff ? (rangeMap[n.first][n.second].second) :
                                  (rangeMap[n.first][n.second].first);
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
      if (check(pos, lastPosMap[n])) {
        current.push_back(interleaving_t(n, pos));
        posMap[n] += increment;
      }
    }

    // FIXME (rkici) : add a check to make sure that the interleaved functions are OK

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

void SDModule::preorderHelper(std::vector<SDModule::vtbl_t>& nodes, const SDModule::vtbl_t& root){
  nodes.push_back(root);
  if (cloudMap.find(root) != cloudMap.end()) {
    for (const SDModule::vtbl_t& n : cloudMap[root]) {
      preorderHelper(nodes, n);
    }
  }
}

std::vector<SDModule::vtbl_t> SDModule::preorder(vtbl_t& root) {
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

static inline SDModule::vtbl_name_t
sd_getStringFromMDTuple(const MDOperand& op) {
  MDString* mds = dyn_cast_or_null<MDString>(op.get());
  assert(mds);

  return mds->getString().str();
}

void SDModule::buildClouds(Module &M) {
  // this set is used for checking if a parent class is defined or not
  std::set<vtbl_name_t> build_undefinedVtables;

  for(auto itr = M.getNamedMDList().begin(); itr != M.getNamedMDList().end(); itr++) {
    NamedMDNode* md = itr;

    // check if this is a metadata that we've added
    if(! md->getName().startswith(SD_MD_CLASSINFO))
      continue;

//    sd_print("GOT METADATA: %s\n", md->getName().data());

    std::vector<nmd_t> infoVec = extractMetadata(md);

    for (const nmd_t& info : infoVec) {
      // record the old vtable array
      GlobalVariable* oldVtable = M.getGlobalVariable(info.className, true);

//      sd_print("oldvtables: %p, %d, class %s\n",
//               oldVtable,
//               oldVtable ? oldVtable->hasInitializer() : -1,
//               info.className.c_str());

      if (oldVtable && oldVtable->hasInitializer()) {
        ConstantArray* vtable = dyn_cast<ConstantArray>(oldVtable->getInitializer());
        assert(vtable);
        oldVTables[info.className] = vtable;
      } else {
        undefinedVTables.insert(info.className);
      }

      // remove the root from undefined vtables
      if (build_undefinedVtables.find(info.className) != build_undefinedVtables.end())
        build_undefinedVtables.erase(info.className);

      for(unsigned ind = 0; ind < info.subVTables.size(); ind++) {
        const nmd_sub_t* subInfo = & info.subVTables[ind];
        vtbl_t name(info.className, ind);

        if (subInfo->parentName != "") {
          // add the current class to the parent's children set
          cloudMap[vtbl_t(subInfo->parentName,0)].insert(name);

          // if the parent class is not defined yet, add it to the
          // undefined vtable set
          if (cloudMap.find(vtbl_t(subInfo->parentName,0)) == cloudMap.end())
            build_undefinedVtables.insert(subInfo->parentName);
        } else {
          assert(ind == 0); // make sure secondary vtables have a direct parent

          if (cloudMap.find(name) == cloudMap.end()){
            // make sure the root is added to the cloud
            cloudMap[name] = std::set<vtbl_t>();
          }

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

  assert(build_undefinedVtables.size() == 0);
}

void SDModule::handleUndefinedVtables(std::set<SDModule::vtbl_name_t>& undefVtbls) {
  for (const vtbl_name_t& vtbl : undefVtbls) {
    sd_print("undefined vtable: %s\n", vtbl.c_str());
  }
}

static llvm::GlobalVariable* sd_mdnodeToGV(Metadata* vtblMd) {
  llvm::MDNode* mdNode = dyn_cast<llvm::MDNode>(vtblMd);
  assert(mdNode);
  Metadata* md = mdNode->getOperand(0).get();
  assert(md);

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

std::vector<SDModule::nmd_t>
SDModule::extractMetadata(NamedMDNode* md) {
  std::set<vtbl_name_t> classes;
  std::vector<SDModule::nmd_t> infoVec;

  unsigned op = 0;

  do {
    SDModule::nmd_t info;
    MDString* infoMDstr = dyn_cast_or_null<MDString>(md->getOperand(op++)->getOperand(0));
    assert(infoMDstr);
    info.className = infoMDstr->getString().str();
    GlobalVariable* classVtbl = sd_mdnodeToGV(md->getOperand(op++));

    if (classVtbl) {
      info.className = classVtbl->getName();
    }
//    sd_print("class: %s\n", info.className.c_str());

    unsigned numOperands = sd_getNumberFromMDTuple(md->getOperand(op++)->getOperand(0));
//    sd_print("operandNo : %u\n", numOperands);

    for (unsigned i = op; i < op + numOperands; ++i) {
      SDModule::nmd_sub_t subInfo;
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

int64_t SDModule::oldIndexToNew2(SDModule::vtbl_t name, int64_t offset,
                                bool isRelative = true) {
  if (! newLayoutInds.count(name)) {
    sd_print("class: (%s, %lu) doesn't belong to newLayoutInds\n", name.first.c_str(), name.second);
    sd_print("%s has %u address points\n", name.first.c_str(), addrPtMap[name.first].size());
    assert(false);
  }

  assert(rangeMap.find(name.first) != rangeMap.end() &&
         rangeMap[name.first].size() > name.second);

  std::vector<uint64_t>& newInds = newLayoutInds[name];
  range_t& subVtableRange = rangeMap[name.first].at(name.second);

  if (isRelative) {
    int64_t oldAddrPt = addrPtMap[name.first].at(name.second) - subVtableRange.first;
    int64_t fullIndex = oldAddrPt + offset;

    if (! (fullIndex >= 0 && fullIndex <= ((int64_t) subVtableRange.second - subVtableRange.first))) {
      sd_print("error in oldIndexToNew2: %s, addrPt:%ld, old:%ld\n", name.first.c_str(), oldAddrPt, offset);
      assert(false);
    }

    return ((int64_t) newInds.at(fullIndex)) - ((int64_t) newInds.at(oldAddrPt));
  } else {
    assert(0 <= offset && offset <= newInds.size());
    return newInds[offset];
  }
}

int64_t SDModule::oldIndexToNew(SDModule::vtbl_name_t vtbl, int64_t offset,
                                bool isRelative = true) {
  vtbl_t name(vtbl,0);

  // if the class doesn't have any vtable defined,
  // use one of its children to calculate function ptr offset
  if (newLayoutInds.find(name) == newLayoutInds.end()) {
    // i don't know if works for negative offsets too
    assert(isRelative && offset >= 0);

    // FIXME (rkici) : so weird, don't know what to do
    assert(cloudMap.find(name) == cloudMap.end());
    return offset;
  }

  return oldIndexToNew2(name, offset, isRelative);
}

uint32_t SDModule::calculateChildrenCounts(const SDModule::vtbl_t& root){
  uint32_t count = 1;
  if (cloudMap.find(root) != cloudMap.end()) {
    for (const SDModule::vtbl_t& n : cloudMap[root]) {
      count += calculateChildrenCounts(n);
    }
  }

  if (root.second == 0) {
    assert(cloudSizeMap.find(root.first) == cloudSizeMap.end());
    cloudSizeMap[root.first] = count;
  }

  return count;
}

void SDModule::clearAnalysisResults() {
  cloudMap.clear();
  roots.clear();
  addrPtMap.clear();
  rangeMap.clear();
  ancestorMap.clear();
  newLayoutInds.clear();
  interleavingMap.clear();
  oldVTables.clear();
  cloudSizeMap.clear();

  sd_print("Cleared SDModule analysis results\n");
}

/// ----------------------------------------------------------------------------
/// SDChangeIndices implementation
/// ----------------------------------------------------------------------------

#include "llvm/ADT/SmallString.h"

static std::string
sd_getClassNameFromMD(llvm::MDNode* mdNode, unsigned operandNo = 0) {
//  llvm::MDTuple* mdTuple = dyn_cast<llvm::MDTuple>(mdNode);
//  assert(mdTuple);
  llvm::MDTuple* mdTuple = cast<llvm::MDTuple>(mdNode);
  assert(mdTuple->getNumOperands() > operandNo + 1);

//  llvm::MDNode* nameMdNode = dyn_cast<llvm::MDNode>(mdTuple->getOperand(operandNo).get());
//  assert(nameMdNode);
  llvm::MDNode* nameMdNode = cast<llvm::MDNode>(mdTuple->getOperand(operandNo).get());

//  llvm::MDString* mdStr = dyn_cast<llvm::MDString>(nameMdNode->getOperand(0));
//  assert(mdStr);
  llvm::MDString* mdStr = cast<llvm::MDString>(nameMdNode->getOperand(0));

  StringRef strRef = mdStr->getString();
  assert(sd_isVtableName_ref(strRef));

//  llvm::MDNode* gvMd = dyn_cast<llvm::MDNode>(mdTuple->getOperand(operandNo+1).get());
  llvm::MDNode* gvMd = cast<llvm::MDNode>(mdTuple->getOperand(operandNo+1).get());

//  SmallString<256> OutName;
//  llvm::raw_svector_ostream Out(OutName);
//  gvMd->print(Out, CURR_MODULE);
//  Out.flush();

  llvm::ConstantAsMetadata* vtblConsMd = dyn_cast_or_null<ConstantAsMetadata>(gvMd->getOperand(0).get());
  if (vtblConsMd == NULL) {
//    llvm::MDNode* tmpnode = dyn_cast<llvm::MDNode>(gvMd);
//    llvm::MDString* tmpstr = dyn_cast<llvm::MDString>(tmpnode->getOperand(0));
//    assert(tmpstr->getString() == "NO_VTABLE");

    return strRef.str();
  }

//  llvm::GlobalVariable* vtbl = dyn_cast<llvm::GlobalVariable>(vtblConsMd->getValue());
//  assert(vtbl);
  llvm::GlobalVariable* vtbl = cast<llvm::GlobalVariable>(vtblConsMd->getValue());

  StringRef vtblNameRef = vtbl->getName();
  assert(vtblNameRef.startswith(strRef));

  return vtblNameRef.str();
}

void SDChangeIndices::updateBasicBlock2(Module* module, BasicBlock &BB) {
  llvm::MDNode* mdNode = NULL;

  std::vector<Instruction*> instructions;
  for(BasicBlock::iterator instItr = BB.begin(); instItr != BB.end(); instItr++) {
    if (instItr->hasMetadataOtherThanDebugLoc())
      instructions.push_back(instItr);
  }

  std::set<Instruction*> changedInstructions;
  this->changedInstructions = &changedInstructions;

  for(std::vector<Instruction*>::iterator instItr = instructions.begin();
      instItr != instructions.end(); instItr++) {
    Instruction* inst = *instItr;
    assert(inst);

    unsigned opcode = inst->getOpcode();

    // gep instruction
    if (opcode == GEP_OPCODE) {
      GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(inst);
      assert(gepInst);

      if((mdNode = inst->getMetadata(classNameMDId))){
        updateVfptrIndex(mdNode, gepInst);
      } else if ((mdNode = inst->getMetadata(vbaseMDId))) {
        std::string className = sd_getClassNameFromMD(mdNode);
        int64_t oldValue = getMetadataConstant(mdNode, 2);
        int64_t oldInd = oldValue / WORD_WIDTH;

        int64_t newInd = sdModule->oldIndexToNew(className,oldInd,true);
        int64_t newValue = newInd * WORD_WIDTH;

        sanityCheck1(gepInst);
        sd_changeGEPIndex(gepInst, 1, newValue);

      } else if ((mdNode = inst->getMetadata(memptrOptMdId))) {
        ConstantInt* ci = dyn_cast<ConstantInt>(inst->getOperand(1));
        if (ci) {
          std::string className = sd_getClassNameFromMD(mdNode);
          // this happens when program is compiled with -O
          // vtable index of the member pointer is put directly into the
          // GEP instruction using constant folding
          int64_t oldValue = ci->getSExtValue();
          int64_t newValue = sdModule->oldIndexToNew(className,oldValue,true);

          sanityCheck1(gepInst);
          sd_changeGEPIndex(gepInst, 1, newValue);
        }
      }
    }

    // call instruction
    else if ((mdNode = inst->getMetadata(castFromMDId))) {
      assert(opcode == CALL_OPCODE);
      replaceDynamicCast(module, inst, mdNode);
    }

    // load instruction
    else if ((mdNode = inst->getMetadata(typeidMDId))) {
      assert(opcode == LOAD_OPCODE);
      updateRTTIOffset(inst,mdNode);
    }

    // bitcast instruction
    else if ((mdNode = inst->getMetadata(vcallMDId))) {
      Function* f = inst->getParent()->getParent();
      assert(sd_isVthunk(f->getName()));
    }

    // store instruction
    else if ((mdNode = inst->getMetadata(memptrMDId))) {
      assert(opcode == STORE_OPCODE);
      handleStoreMemberPointer(mdNode, inst);
    }

    // select instruction
    else if ((mdNode = inst->getMetadata(memptr2MDId))) {
      assert(opcode == SELECT_OPCODE);
      handleSelectMemberPointer(mdNode, inst);
    }

    else if ((mdNode = inst->getMetadata(checkMdId))) {
      assert(opcode == ICMP_OPCODE);
      handleVtableCheck(module, mdNode, inst);
    }
  }
}

bool SDChangeIndices::updateBasicBlock(Module* module, BasicBlock &BB) {
  llvm::MDNode* mdNode = NULL;

  std::vector<Instruction*> instructions;
  for(BasicBlock::iterator instItr = BB.begin(); instItr != BB.end(); instItr++) {
    if (instItr->hasMetadataOtherThanDebugLoc())
      instructions.push_back(instItr);
  }

  std::set<Instruction*> changedInstructions;
  this->changedInstructions = &changedInstructions;

  for(std::vector<Instruction*>::iterator instItr = instructions.begin();
      instItr != instructions.end(); instItr++) {
    Instruction* inst = *instItr;
    assert(inst);

    unsigned opcode = inst->getOpcode();

    // gep instruction
    if (opcode == GEP_OPCODE) {
      GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(inst);
      assert(gepInst);

      if((mdNode = inst->getMetadata(classNameMDId))){
        updateVfptrIndex(mdNode, gepInst);

      } else if ((mdNode = inst->getMetadata(vbaseMDId))) {
        std::string className = sd_getClassNameFromMD(mdNode);
        int64_t oldValue = getMetadataConstant(mdNode, 2);
        int64_t oldInd = oldValue / WORD_WIDTH;

//        sd_print("vbase: class: %s, old: %d\n", className.c_str(), oldInd);
        int64_t newInd = sdModule->oldIndexToNew(className,oldInd,true);
        int64_t newValue = newInd * WORD_WIDTH;

        sanityCheck1(gepInst);
        sd_changeGEPIndex(gepInst, 1, newValue);

      } else if ((mdNode = inst->getMetadata(memptrOptMdId))) {
        ConstantInt* ci = dyn_cast<ConstantInt>(inst->getOperand(1));
        if (ci) {
          std::string className = sd_getClassNameFromMD(mdNode);
          // this happens when program is compiled with -O
          // vtable index of the member pointer is put directly into the
          // GEP instruction using constant folding
          int64_t oldValue = ci->getSExtValue();

//          sd_print("memptr opt: class: %s, old: %d\n", className.c_str(), oldValue);
          int64_t newValue = sdModule->oldIndexToNew(className,oldValue,true);

          sanityCheck1(gepInst);
          sd_changeGEPIndex(gepInst, 1, newValue);
        }
      }
    }

    // call instruction
    else if ((mdNode = inst->getMetadata(castFromMDId))) {
      assert(opcode == CALL_OPCODE);
      replaceDynamicCast(module, inst, mdNode);
    }

    // load instruction
    else if ((mdNode = inst->getMetadata(typeidMDId))) {
      assert(opcode == LOAD_OPCODE);
      updateRTTIOffset(inst,mdNode);
    }

    // bitcast instruction
    else if ((mdNode = inst->getMetadata(vcallMDId))) {
      Function* f = inst->getParent()->getParent();
      assert(sd_isVthunk(f->getName()));
    }

    // store instruction
    else if ((mdNode = inst->getMetadata(memptrMDId))) {
      assert(opcode == STORE_OPCODE);
      handleStoreMemberPointer(mdNode, inst);
    }

    // select instruction
    else if ((mdNode = inst->getMetadata(memptr2MDId))) {
      assert(opcode == SELECT_OPCODE);
      handleSelectMemberPointer(mdNode, inst);
    }

    else if ((mdNode = inst->getMetadata(checkMdId))) {
      assert(opcode == ICMP_OPCODE);
      handleVtableCheck(module, mdNode, inst);
    }
  }

  return true;
}

void SDChangeIndices::updateVfptrIndex(MDNode *mdNode, Instruction *inst) {
//  GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(inst);
//  assert(gepInst);

  GetElementPtrInst* gepInst = cast<GetElementPtrInst>(inst);

  std::string className = sd_getClassNameFromMD(mdNode);

  ConstantInt* index = cast<ConstantInt>(gepInst->getOperand(1));
  uint64_t indexVal = index->getSExtValue();

  uint64_t newIndexVal = sdModule->oldIndexToNew(className, indexVal, true);

  gepInst->setOperand(1, ConstantInt::get(IntegerType::getInt64Ty(gepInst->getContext()),
                                          newIndexVal, false));

  sanityCheck1(gepInst);
}

void SDChangeIndices::replaceDynamicCast(Module *module, Instruction *inst, llvm::MDNode* mdNode) {
  Function* from = module->getFunction("__dynamic_cast");

  if (from == NULL)
    return;

  std::string className = sd_getClassNameFromMD(mdNode);

  // in LLVM, we cannot call a function declared outside of the module
  // so add a declaration here
  LLVMContext& context = module->getContext();
  FunctionType* dyncastFunType = getDynCastFunType(context);
  Constant* dyncastFun = module->getOrInsertFunction(SD_DYNCAST_FUNC_NAME,
                                                     dyncastFunType);

  Function* dyncastFunF = cast<Function>(dyncastFun);

  // create the argument list for calling the function
  std::vector<Value*> arguments;
  CallInst* callInst = dyn_cast<CallInst>(inst);
  assert(callInst);

  assert(callInst->getNumArgOperands() == 4);
  for (unsigned argNo = 0; argNo < callInst->getNumArgOperands(); ++argNo) {
    arguments.push_back(callInst->getArgOperand(argNo));
  }

//  sd_print("dyncast: %s (-1 & -2) \n", className.c_str());
  int64_t newOTTOff  = sdModule->oldIndexToNew(className, -2, true);
  int64_t newRTTIOff = sdModule->oldIndexToNew(className, -1, true);

  arguments.push_back(ConstantInt::get(context, APInt(64, newRTTIOff * WORD_WIDTH, true))); // rtti
  arguments.push_back(ConstantInt::get(context, APInt(64, newOTTOff * WORD_WIDTH, true))); // ott

  sanityCheck1(callInst);

  bool isReplaced = sd_replaceCallFunctionWith(callInst, dyncastFunF, arguments);
  assert(isReplaced);
}

void SDChangeIndices::updateRTTIOffset(Instruction *inst, llvm::MDNode* mdNode) {
  std::string className = sd_getClassNameFromMD(mdNode);

//  sd_print("rtti: %s -1\n", className.c_str());
  int64_t newRttiOff = sdModule->oldIndexToNew(className, -1, true);

  LoadInst* loadInst = dyn_cast<LoadInst>(inst);
  assert(loadInst);
  GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(loadInst->getOperand(0));
  assert(gepInst);

  sanityCheck1(gepInst);
  sd_changeGEPIndex(gepInst, 1, newRttiOff);
}

void SDChangeIndices::replaceConstantStruct(ConstantStruct *CS, Instruction *inst, std::string& className) {
  std::vector<Constant*> V;
  ConstantInt* ci = dyn_cast<ConstantInt>(CS->getOperand(0));
  assert(ci);

  int64_t oldValue = (ci->getSExtValue() - 1) / WORD_WIDTH;

//  sd_print("ConsStruct: %s %ld\n", className.c_str(), oldValue);
  int64_t newValue = sdModule->oldIndexToNew(className, oldValue, true);

  V.push_back(ConstantInt::get(
                Type::getInt64Ty(inst->getContext()),
                newValue * WORD_WIDTH + 1));

  ci = dyn_cast<ConstantInt>(CS->getOperand(1));
  assert(ci);

  V.push_back(ConstantInt::get(
                Type::getInt64Ty(inst->getContext()),
                ci->getSExtValue()));

  Constant* CSNew = ConstantStruct::getAnon(V);
  assert(CSNew);

  inst->replaceUsesOfWith(CS,CSNew);
}

void SDChangeIndices::handleStoreMemberPointer(MDNode *mdNode, Instruction *inst){
  std::string className = sd_getClassNameFromMD(mdNode);

  StoreInst* storeInst = dyn_cast<StoreInst>(inst);
  assert(storeInst);

  ConstantStruct* CS = dyn_cast<ConstantStruct>(storeInst->getOperand(0));
  assert(CS);

  sanityCheck1(inst);

  replaceConstantStruct(CS, storeInst, className);
}

void SDChangeIndices::handleVtableCheck_1(Module* M, MDNode *mdNode, Instruction *inst){
  sanityCheck1(inst);

  std::string className = sd_getClassNameFromMD(mdNode);
  SDModule::vtbl_t vtbl(className,0);

  // %134 = icmp ult i64 %133, <count>, !dbg !874, !sd.check !751
  ICmpInst* icmpInst = dyn_cast<ICmpInst>(inst);
  assert(icmpInst);

  // %133 = sub i64 %132, <vtbl address>, !dbg !874
  SubOperator* subOp = dyn_cast<SubOperator>(icmpInst->getOperand(0));
  assert(subOp);

  // %132 = ptrtoint void (%class.A*)** %vtable90 to i64, !dbg !874
  PtrToIntInst* ptiInst = dyn_cast<PtrToIntInst>(subOp->getOperand(0));
  assert(ptiInst);

  LLVMContext& C = M->getContext();

  if (sdModule->cloudMap.count(vtbl) == 0 &&
      sdModule->newLayoutInds.count(vtbl) == 0) {
    // FIXME (rkici) : temporarily fix these weird classes by making it always check
    Value* zero = ConstantInt::get(IntegerType::getInt64Ty(C), 0);
    icmpInst->setOperand(1, zero);
    subOp->setOperand(1, subOp->getOperand(0));

    return;
  }

  // change the start address of the root class
  Value* addrPt = sdModule->newVtblAddress(*M, className, ptiInst);
  subOp->setOperand(1, addrPt);

  assert(sdModule->cloudSizeMap.count(className));
  uint32_t cloudSize = sdModule->cloudSizeMap[className];
  cloudSize *= WORD_WIDTH;

  Value* cloudSizeVal = ConstantInt::get(IntegerType::getInt64Ty(C), cloudSize);

  // update the cloud range
  icmpInst->setOperand(1, cloudSizeVal);
}

void SDChangeIndices::handleVtableCheck_2(Module* M, MDNode *mdNode, Instruction *inst){
  sanityCheck1(inst);

  std::string className = sd_getClassNameFromMD(mdNode);
  SDModule::vtbl_t vtbl(className,0);

  // %134 = icmp ult i64 %133, <count>, !dbg !874, !sd.check !751
  ICmpInst* icmpInst = dyn_cast<ICmpInst>(inst);
  assert(icmpInst);
  CmpInst::Predicate pred = icmpInst->getPredicate();

  assert(pred == CmpInst::ICMP_UGE || pred == CmpInst::ICMP_ULE);

  // %132 = ptrtoint void (%class.A*)** %vtable90 to i64, !dbg !874
  PtrToIntInst* ptiInst = dyn_cast<PtrToIntInst>(icmpInst->getOperand(0));
  assert(ptiInst);

  LLVMContext& C = M->getContext();

  if (sdModule->cloudMap.count(vtbl) == 0 &&
      sdModule->newLayoutInds.count(vtbl) == 0) {
    // FIXME (rkici) : temporarily fix these weird classes by making it always check
    icmpInst->setOperand(1,ptiInst);
    return;
  }

  // change the start address of the root class
  Value* addrPt = sdModule->newVtblAddress(*M, className, ptiInst);

  assert(sdModule->cloudSizeMap.count(className));
  uint32_t cloudSize = sdModule->cloudSizeMap[className];
  cloudSize *= WORD_WIDTH;

  if (cloudSize == WORD_WIDTH) {
    // if there is only one class in the cloud, use equality for the first check
    // and remove the 2nd one
    if (pred == CmpInst::ICMP_UGE) {
      icmpInst->setPredicate(CmpInst::ICMP_EQ);
      icmpInst->setOperand(1, addrPt);
    } else {
      assert(! icmpInst->use_empty());
      Value::use_iterator itr = icmpInst->use_begin();
      Value* v = ((Use&) *itr).getUser();
      BranchInst* brInst = dyn_cast<BranchInst>(v);
      if (brInst == NULL) {
        v->dump();
        assert(false);
      }
      brInst->setCondition(ConstantInt::getTrue(C));
      icmpInst->eraseFromParent();
    }
    return;
  }

  if (pred == CmpInst::ICMP_UGE) {
    // icmp uge vtable, cloud start
    icmpInst->setOperand(1, addrPt);
  } else {
    Value* cloudSizeVal = ConstantInt::get(IntegerType::getInt64Ty(C), cloudSize);
    IRBuilder<> builder(icmpInst);
    builder.SetInsertPoint(icmpInst);
    Value* cloudEnd = builder.CreateAdd(addrPt, cloudSizeVal);

    // update the cloud range
    icmpInst->setOperand(1, cloudEnd);
  }
}

void SDChangeIndices::handleSelectMemberPointer(MDNode *mdNode, Instruction *inst){
  std::string className1 = sd_getClassNameFromMD(mdNode,0);
  std::string className2 = sd_getClassNameFromMD(mdNode,2);

  SelectInst* selectInst = dyn_cast<SelectInst>(inst);
  assert(selectInst);

  sanityCheck1(inst);

  ConstantStruct* CS1 = dyn_cast<ConstantStruct>(selectInst->getOperand(1));
  assert(CS1);
  replaceConstantStruct(CS1, selectInst, className1);

  ConstantStruct* CS2 = dyn_cast<ConstantStruct>(selectInst->getOperand(2));
  assert(CS2);
  replaceConstantStruct(CS2, selectInst, className2);
}

int64_t SDChangeIndices::getMetadataConstant(llvm::MDNode *mdNode, unsigned operandNo) {
  llvm::MDTuple* mdTuple = dyn_cast<llvm::MDTuple>(mdNode);
  assert(mdTuple);

  llvm::ConstantAsMetadata* constantMD = dyn_cast_or_null<ConstantAsMetadata>(
        mdTuple->getOperand(operandNo));
  assert(constantMD);

  ConstantInt* constantInt = dyn_cast<ConstantInt>(constantMD->getValue());
  assert(constantInt);

  return constantInt->getSExtValue();
}

FunctionType *SDChangeIndices::getDynCastFunType(LLVMContext &context) {
  std::vector<Type*> argVector;
  argVector.push_back(Type::getInt8PtrTy(context)); // object address
  argVector.push_back(Type::getInt8PtrTy(context)); // type of the starting object
  argVector.push_back(Type::getInt8PtrTy(context)); // desired target type
  argVector.push_back(Type::getInt64Ty(context));   // src2det ptrdiff
  argVector.push_back(Type::getInt64Ty(context));   // rttiOff ptrdiff
  argVector.push_back(Type::getInt64Ty(context));   // ottOff  ptrdiff

  return FunctionType::get(Type::getInt8PtrTy(context),
                           argVector, false);
}

/// ----------------------------------------------------------------------------
/// Helper functions
/// ----------------------------------------------------------------------------

bool llvm::sd_replaceCallFunctionWith(CallInst* callInst, Function* to, std::vector<Value*> args) {
  assert(callInst && to && (args.size() > 0));

  IRBuilder<> builder(callInst);
  builder.SetInsertPoint(callInst);
  CallInst* newCall = builder.CreateCall(to, args, "sd.new_dyncast");

  newCall->setAttributes(callInst->getAttributes());
  callInst->replaceAllUsesWith(newCall);
  callInst->eraseFromParent();

  return true;
}

void llvm::sd_changeGEPIndex(GetElementPtrInst* inst, unsigned operandNo, int64_t newIndex) {
  assert(inst);

  Value *idx = ConstantInt::getSigned(Type::getInt64Ty(inst->getContext()), newIndex);
  inst->setOperand(operandNo, idx);
}

// folder creation
#include <deque>

void SDModule::printClouds() {
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


void SDModule::removeVtablesAndThunks(Module &M) {
  for (auto itr : oldVTables) {
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
