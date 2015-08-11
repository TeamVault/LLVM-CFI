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
#include "llvm/IR/Intrinsics.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
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

#include <iostream>

namespace {

  /**
   * Module pass for the SafeDispatch Gold Plugin
   */
  class SDModule : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid

    SDModule() : ModulePass(ID) {
      std::cerr << "Creating SDModule pass!\n";
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
    typedef std::map<vtbl_t, std::map<uint64_t, uint64_t>>  new_layout_inds_map_t;
    typedef std::pair<vtbl_t, uint64_t>       					    interleaving_t;
    typedef std::list<interleaving_t>                       interleaving_list_t;
    typedef std::map<vtbl_name_t, interleaving_list_t>      interleaving_map_t;
    typedef std::vector<vtbl_t>                             order_t;
    typedef std::map<vtbl_name_t, std::vector<vtbl_name_t>> subvtbl_map_t;
    typedef std::map<vtbl_name_t, ConstantArray*>           oldvtbl_map_t;
    typedef std::map<vtbl_t, Constant*>                     vtbl_start_map_t;
    typedef std::map<vtbl_name_t, GlobalVariable*>          cloud_start_map_t;

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
    vtbl_start_map_t newVTableStartAddrMap;            // Starting addresses of all new vtables
    cloud_start_map_t cloudStartMap;                   // Mapping from new vtable names to their corresponding cloud starts

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
      sd_print("Started safedispatch analysis\n");

      vcallMDId = M.getMDKindID(SD_MD_VCALL);

      buildClouds(M);          // part 1
      //printClouds();
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

    Constant* newVtblAddressConst(Module& M, const vtbl_t& vtbl) {
      const DataLayout &DL = M.getDataLayout();
      assert(ancestorMap.count(vtbl));
      vtbl_name_t rootName = ancestorMap[vtbl];
      LLVMContext& C = M.getContext();
      Type *IntPtrTy = DL.getIntPtrType(C);

      // sanity checks
      assert(roots.count(rootName));

      // switch to the new vtable name
      rootName = NEW_VTABLE_NAME(rootName);

      // we should add the address point of the given class
      // inside the new interleaved vtable to the start address
      // of the vtable

      // find which element is the address point
      assert(addrPtMap.count(vtbl.first));
      unsigned addrPt = addrPtMap[vtbl.first][vtbl.second] - rangeMap[vtbl.first][vtbl.second].first;

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

    void removeVtablesAndThunks(Module &M);

    bool isUndefined(const vtbl_name_t &vtbl) {
      return undefinedVTables.find(vtbl) != undefinedVTables.end();
    }

    bool isUndefined(const vtbl_t &vtbl) {
      return isUndefined(vtbl.first);
    }

    bool isDefined(const vtbl_t &vtbl) {
      return !isUndefined(vtbl);
    }

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
        assert(verifyInterleavedCloud(vtbl));
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
     * Interleave the cloud given by the root element.
     */
    void interleaveCloud(vtbl_name_t& vtbl);

    bool verifyInterleavedCloud(vtbl_name_t& vtbl);

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
     * Extract the vtable info from the metadata and put it into a struct
     */
    std::vector<nmd_t> static extractMetadata(NamedMDNode* md);
    /**
     * Return a list that contains the preorder traversal of the tree
     * starting from the given node
     */
    order_t preorder(const vtbl_t& root);
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

    /**
     * Return the number of vtables in a given primary vtable's cloud(including
     * the vtable itself). This is effectively the width of the range in which
     * the vtable pointer must lie in.
     */
    int64_t getCloudSize(const vtbl_name_t& vtbl);
    /**
     * Get the start of the valid range for vptrs for a (potentially non-primary) vtable.
     * In practice we are always interested in primary vtables here.
     */
    llvm::Constant* getVTableRangeStart(const vtbl_t& vtbl);
    llvm::Constant* getVTableRangeEnd(const vtbl_t& vtbl, const SDModule::vtbl_name_t &rootName);
    vtbl_t getFirstDefinedChild(const vtbl_t &vtbl);
    bool hasDefinedChild(const vtbl_t &vtbl);
    bool knowsAbout(const vtbl_t &vtbl); // Have we ever seen md about this vtable?
    bool validConstVptr(const vtbl_name_t &v, const DataLayout &DL, Value *V,
        uint64_t off);
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
      sdModule = &getAnalysis<SDModule>();
      assert(sdModule);

      sd_print("inside the 2nd pass\n");

      nConstTrue = 0;
      nConstFalse = 0;
      nEq = 0;
      nRange = 0;

      classNameMDId = M.getMDKindID(SD_MD_VFUN_CALL);
      castFromMDId  = M.getMDKindID(SD_MD_CAST_FROM);
      typeidMDId    = M.getMDKindID(SD_MD_TYPEID);
      vcallMDId     = M.getMDKindID(SD_MD_VCALL);
      vbaseMDId     = M.getMDKindID(SD_MD_VBASE);
      memptrMDId    = M.getMDKindID(SD_MD_MEMPTR);
      memptr2MDId   = M.getMDKindID(SD_MD_MEMPTR2);
      memptrOptMdId = M.getMDKindID(SD_MD_MEMPTR_OPT);
      checkMdId     = M.getMDKindID(SD_MD_CHECK);

      std::cerr << "Before change indices=============================\n";
//      for(Module::iterator f_itr =M.begin(); f_itr != M.end(); f_itr++) {
//        if (f_itr->getName().str() == "_ZN11EtherAppCli10sendPacketEv") {
//          f_itr->dump();
//        }
//      }

      handleSDGetVtblIndex(&M);
      handleSDGetCheckedVPtr(&M);
      handleSDCheckVtbl(&M);
      handleRemainingSDGetVcallIndex(&M);

      std::cerr << "After change indices===============================\n";
//      for(Module::iterator f_itr =M.begin(); f_itr != M.end(); f_itr++) {
//        if (f_itr->getName().str() == "_ZN11EtherAppCli10sendPacketEv") {
//          f_itr->dump();
//        }
//      }
      sd_print("Finished running the 2nd pass...\n");

      sdModule->removeVtablesAndThunks(M);

      sdModule->clearAnalysisResults();

      sd_print("removed thunks...\n");
      sd_print("Summary: const true: %d const false: %d eq check: %d range check %d\n",
        nConstTrue, nConstFalse, nEq, nRange);

      return true;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<SDModule>();
    }

  private:

    SDModule* sdModule;

    uint64_t nConstTrue;
    uint64_t nConstFalse;
    uint64_t nEq;
    uint64_t nRange;

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

    void handleSDGetVtblIndex(Module* M);
    void handleSDCheckVtbl(Module* M);
    void handleSDGetCheckedVPtr(Module* M);
    void handleRemainingSDGetVcallIndex(Module* M);
  };
}

  /**
   * Module pass for substittuing the final subst_ intrinsics
   */
  class SDSubstModule : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid

    SDSubstModule() : ModulePass(ID) {
      initializeSDSubstModulePass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDSubstModule() {
      sd_print("deleting SDSubstModule pass\n");
    }

    bool runOnModule(Module &M) {
      int64_t indexSubst = 0, rangeSubst = 0, eqSubst = 0, constPtr = 0;
      Function *sd_subst_indexF =
          M.getFunction(Intrinsic::getName(Intrinsic::sd_subst_vtbl_index));
       Function *sd_subst_rangeF =
          M.getFunction(Intrinsic::getName(Intrinsic::sd_subst_check_range));

      /*
      std::cerr << "Before subst=============================\n";
      for(Module::iterator f_itr =M.begin(); f_itr != M.end(); f_itr++) {
        if (f_itr->getName().str() == "_ZN11EtherAppCli10sendPacketEv") {
          f_itr->dump();
        }
      }
      */


      if (sd_subst_indexF) {
        for (const Use &U : sd_subst_indexF->uses()) {
          // get the call inst
          llvm::CallInst* CI = cast<CallInst>(U.getUser());

          // get the arguments
          llvm::ConstantInt* arg1 = dyn_cast<ConstantInt>(CI->getArgOperand(0));
          assert(arg1);
          CI->replaceAllUsesWith(arg1);
          CI->eraseFromParent();
          indexSubst += 1;
        }
      }

      if (sd_subst_rangeF) {
        const DataLayout &DL = M.getDataLayout();
        LLVMContext& C = M.getContext();
        Type *IntPtrTy = DL.getIntPtrType(C, 0);

        for (const Use &U : sd_subst_rangeF->uses()) {
          // get the call inst
          llvm::CallInst* CI = cast<CallInst>(U.getUser());
          IRBuilder<> builder(CI);
          // get the arguments
          llvm::Value* vptr = CI->getArgOperand(0);
          llvm::Constant* start = dyn_cast<Constant>(CI->getArgOperand(1));
          llvm::ConstantInt* width = dyn_cast<ConstantInt>(CI->getArgOperand(2));
          assert(vptr && start && width);

          llvm::Constant* rootVtblInt = dyn_cast<llvm::Constant>(start->getOperand(0));
          llvm::GlobalVariable* rootVtbl = dyn_cast<llvm::GlobalVariable>(
            rootVtblInt->getOperand(0));
          llvm::ConstantInt* startOff = dyn_cast<llvm::ConstantInt>(start->getOperand(1));

          int64_t widthInt = width->getSExtValue();

          if (validConstVptr(rootVtbl, startOff->getSExtValue(), widthInt, DL, vptr, 0)) {
            CI->replaceAllUsesWith(llvm::ConstantInt::getTrue(C));
            CI->eraseFromParent();
            constPtr++;
          } else
          if (widthInt > 1) {
            // Rotate right by 3 to push the lowest order bits into the higher order bits
            llvm::Value *vptrInt = builder.CreatePtrToInt(vptr, IntPtrTy);
            llvm::Value *diff = builder.CreateSub(vptrInt, start);
            llvm::Value *diffShr = builder.CreateLShr(diff, 3);
            llvm::Value *diffShl = builder.CreateShl(diff, DL.getPointerSizeInBits(0) - 3);
            llvm::Value *diffRor = builder.CreateOr(diffShr, diffShl);

            llvm::Value *inRange = builder.CreateICmpULE(diffRor, width);
              
            CI->replaceAllUsesWith(inRange);
            CI->eraseFromParent();

            rangeSubst += 1;
          } else {
            llvm::Value *vptrInt = builder.CreatePtrToInt(vptr, IntPtrTy);
            llvm::Value *inRange = builder.CreateICmpEQ(vptrInt, start);

            CI->replaceAllUsesWith(inRange);
            CI->eraseFromParent();
            eqSubst += 1;
          }        
        }
      }

      /*
      std::cerr << "After subst=============================\n";
      for(Module::iterator f_itr =M.begin(); f_itr != M.end(); f_itr++) {
        if (f_itr->getName().str() == "_ZN11EtherAppCli10sendPacketEv") {
          f_itr->dump();
        }
      }
      */
      sd_print("SDSubst: indices: %d ranges: %d eq_checks: %d const_ptr: %d\n", indexSubst, rangeSubst, eqSubst, constPtr);
      return indexSubst > 0 || rangeSubst > 0 || eqSubst > 0 || constPtr > 0;
    }

    bool isConstVptr(const DataLayout &DL, Value *V,uint64_t off);
    bool validConstVptr(GlobalVariable* rootVtbl, int64_t start, int64_t width,
        const DataLayout &DL, Value *V, uint64_t off);
  };

  // This code is adapted from LowerBitSets.cpp
  bool SDSubstModule::isConstVptr(const DataLayout &DL, Value *V,
      uint64_t off) {
    if (dyn_cast<GlobalVariable>(V)) {
      return true;
    }

    if (auto GEP = dyn_cast<GEPOperator>(V)) {
      APInt APOffset(DL.getPointerSizeInBits(0), 0);
      bool Result = GEP->accumulateConstantOffset(DL, APOffset);
      if (!Result)
        return false;

      off += APOffset.getZExtValue();
      return isConstVptr(DL, GEP->getPointerOperand(), off);
    }

    if (auto Op = dyn_cast<Operator>(V)) {
      if (Op->getOpcode() == Instruction::BitCast)
        return isConstVptr(DL, Op->getOperand(0), off);

      if (Op->getOpcode() == Instruction::Select)
        return isConstVptr(DL, Op->getOperand(1), off) &&
               isConstVptr(DL, Op->getOperand(2), off);
    }

    return false;
  }

  /**
   * Module pass for printing the SafeDispatch metadata
   */
  class SDPrintMDModule : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid

    SDPrintMDModule() : ModulePass(ID) {
      initializeSDPrintMDModulePass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDPrintMDModule() {
      sd_print("deleting SDPrintMDModule pass\n");
    }

    bool runOnModule(Module &M) {
      int64_t nodeCnt = 0;

      for(auto itr = M.getNamedMDList().begin(); itr != M.getNamedMDList().end(); itr++) {
        // check if this is a metadata that we've added
        if(itr->getName().startswith(SD_MD_CLASSINFO))
          nodeCnt += 1;
      }

      sd_print("Metadata(%d): ----------------- \n", nodeCnt);

      for(auto itr = M.getNamedMDList().begin(); itr != M.getNamedMDList().end(); itr++) {
        NamedMDNode* md = itr;

        // check if this is a metadata that we've added
        if(! md->getName().startswith(SD_MD_CLASSINFO))
          continue;

        sd_print("Node: %s\n", md->getName().data());

        std::vector<SDModule::nmd_t> infoVec = SDModule::extractMetadata(md);

        for (const SDModule::nmd_t& info : infoVec) {
          sd_print("  VTable: %s\n", info.className.c_str());

          for(unsigned ind = 0; ind < info.subVTables.size(); ind++) {
            const SDModule::nmd_sub_t* subInfo = & info.subVTables[ind];
            sd_print("    [%d]: %s (%d-%d) addrPt: %d\n",
                subInfo->order, subInfo->parentName.c_str(),
                subInfo->start, subInfo->end, subInfo->addressPoint);
          }
        }
      }
      sd_print("---------------------------\n");

      return false;
    }
  };

char SDChangeIndices::ID = 0;
char SDModule::ID = 0;
char SDPrintMDModule::ID = 0;
char SDSubstModule::ID = 0;

INITIALIZE_PASS(SDModule, "sdmp", "Module pass for SafeDispatch", false, false)
INITIALIZE_PASS(SDPrintMDModule, "sdpmdmp", "Module pass for printing SafeDispatch metadata", false, false)
INITIALIZE_PASS(SDSubstModule, "sdsdmp", "Module pass for substituting the constant-holding intrinsics generated by sdmp.", false, false)

INITIALIZE_PASS_BEGIN(SDChangeIndices, "cc", "Change Constant", false, false)
INITIALIZE_PASS_DEPENDENCY(SDModule)
INITIALIZE_PASS_END(SDChangeIndices, "cc", "Change Constant", false, false)


ModulePass* llvm::createSDChangeIndicesPass() {
  return new SDChangeIndices();
}

ModulePass* llvm::createSDModulePass() {
  return new SDModule();
}

ModulePass* llvm::createSDPrintMDModulePass() {
  return new SDPrintMDModule();
}

ModulePass* llvm::createSDSubstModulePass() {
  return new SDSubstModule();
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

// This code is adapted from LowerBitSets.cpp
bool SDModule::validConstVptr(const vtbl_name_t &v, const DataLayout &DL, Value *V,
    uint64_t off) {
  if (auto GV = dyn_cast<GlobalVariable>(V)) {
    vtbl_t t(v, 0);
    vtbl_name_t root = ancestorMap[t];

    if (GV != cloudStartMap[NEW_VTABLE_NAME(root)])
      return false;

    if (off % 8 != 0)
      return false;

    int64_t relStart = newLayoutInds[t][addrPtMap[v][0]];
    int64_t relEnd = relStart + cloudSizeMap[v];
    uint64_t ind = off/8;
    return relStart <= ind && ind < relEnd;
  }

  if (auto GEP = dyn_cast<GEPOperator>(V)) {
    APInt APOffset(DL.getPointerSizeInBits(0), 0);
    bool Result = GEP->accumulateConstantOffset(DL, APOffset);
    if (!Result)
      return false;

    off += APOffset.getZExtValue();
    return validConstVptr(v, DL, GEP->getPointerOperand(), off);
  }

  if (auto Op = dyn_cast<Operator>(V)) {
    if (Op->getOpcode() == Instruction::BitCast)
      return validConstVptr(v, DL, Op->getOperand(0), off);

    if (Op->getOpcode() == Instruction::Select)
      return validConstVptr(v, DL, Op->getOperand(1), off) &&
             validConstVptr(v, DL, Op->getOperand(2), off);
  }

  return false;
}

bool SDSubstModule::validConstVptr(GlobalVariable *rootVtbl, int64_t start, int64_t width,
    const DataLayout &DL, Value *V, uint64_t off) {
  if (auto GV = dyn_cast<GlobalVariable>(V)) {
    if (GV != rootVtbl)
      return false;

    if (off % 8 != 0)
      return false;

    return start <= off && off < (start + width * 8);
  }

  if (auto GEP = dyn_cast<GEPOperator>(V)) {
    APInt APOffset(DL.getPointerSizeInBits(0), 0);
    bool Result = GEP->accumulateConstantOffset(DL, APOffset);
    if (!Result)
      return false;

    off += APOffset.getZExtValue();
    return validConstVptr(rootVtbl, start, width, DL, GEP->getPointerOperand(), off);
  }

  if (auto Op = dyn_cast<Operator>(V)) {
    if (Op->getOpcode() == Instruction::BitCast)
      return validConstVptr(rootVtbl, start, width, DL, Op->getOperand(0), off);

    if (Op->getOpcode() == Instruction::Select)
      return validConstVptr(rootVtbl, start, width, DL, Op->getOperand(1), off) &&
             validConstVptr(rootVtbl, start, width, DL, Op->getOperand(2), off);
  }

  return false;
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

  LLVMContext& C = M.getContext();

  Function *sd_vcall_indexF =
      M.getFunction(Intrinsic::getName(Intrinsic::sd_get_vcall_index));

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
            int64_t newIndex = oldIndexToNew2(vtbl_t(vtbl,order), oldIndex, true);

            Value* newValue = ConstantInt::get(IntegerType::getInt64Ty(C), newIndex * WORD_WIDTH);

            CI->replaceAllUsesWith(newValue);

          }
        }
      }

      // this function should have a metadata
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

/**
* Check that the interleaving for a given cloud:
*    1) Contains all indices for all original (defined) vtables
*    2) For every class C
*        For each subclass D of C
*          For each element in C's vtable
*            The corresponding element in D's vtable is at the same relative offset from the address point
*/
bool SDModule::verifyInterleavedCloud(vtbl_name_t& vtbl) {
  vtbl_t root(vtbl,0);
  assert(interleavingMap.count(vtbl) && roots.count(vtbl) && cloudMap.count(root));

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

  order_t cloud = preorder(root);

  // 1) Check that we have a (DENSE!) map of indices for each vtable in the cloud in the
  // current interleaving. (i.e. inside indMap)
  for (const SDModule::vtbl_t& n : cloud) {
    // Skip undefined vtables
    if (isUndefined(n.first)) {
      // TODO: Assert that it does not appear in the interleaved vtable.
      continue;
    }

    if (indMap.find(n) == indMap.end()) {
        std::cerr << "In ivtbl " << vtbl << " missing " << n.first << "," << n.second << std::endl;
        return false;
    }

    // Check that the index map is dense (total on the range of indices)
    int64_t oldVtblSize = rangeMap[n.first][n.second].second - rangeMap[n.first][n.second].first + 1;
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

  return true;
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
                                                 GlobalVariable::InternalLinkage,
                                                 nullptr, NEW_VTABLE_NAME(vtbl));
  newVtable->setAlignment(WORD_WIDTH);
  newVtable->setInitializer(newVtableInit);
  newVtable->setUnnamedAddr(true);

  cloudStartMap[NEW_VTABLE_NAME(vtbl)] = newVtable;

  // to start changing the original uses of the vtables, first get all the classes in the cloud
  order_t cloud;
  vtbl_t root(vtbl,0);
  preorderHelper(cloud, root);

  Constant* zero = ConstantInt::get(M.getContext(), APInt(64, 0));
  for (const vtbl_t& v : cloud) {
    if (isDefined(v)) {
      assert(newVTableStartAddrMap.find(v) == newVTableStartAddrMap.end());
      newVTableStartAddrMap[v] = newVtblAddressConst(M, v);
    }

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
      if (!isUndefined(n.first) && check(pos, lastPosMap[n])) {
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

std::vector<SDModule::vtbl_t> SDModule::preorder(const vtbl_t& root) {
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

      for(unsigned ind = 0; ind < info.subVTables.size(); ind++) {
        const nmd_sub_t* subInfo = & info.subVTables[ind];
        vtbl_t name(info.className, ind);

        if (ind == 0) {
          // remove the primary vtable from the build_undefined vtables map
          if (build_undefinedVtables.find(info.className) != build_undefinedVtables.end()) {
            build_undefinedVtables.erase(info.className);
          }

          if (cloudMap.find(name) == cloudMap.end()){
            cloudMap[name] = std::set<vtbl_t>();
          }
        }

        if (subInfo->parentName != "") {
          vtbl_t parent(subInfo->parentName, 0);

          // if the parent class is not defined yet, add it to the
          // undefined vtable set
          if (cloudMap.find(parent) == cloudMap.end()) {
            cloudMap[parent] = std::set<vtbl_t>();
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

  sd_print("# Classes: %u (known), %u (unknown)\n", oldVTables.size(), undefinedVTables.size());
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

    unsigned numOperands = sd_getNumberFromMDTuple(md->getOperand(op++)->getOperand(0));

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

  if (isUndefined(name) && hasDefinedChild(name)) {
    name = getFirstDefinedChild(name);
  }

  // if the class doesn't have any vtable defined,
  // use one of its children to calculate function ptr offset
  if (newLayoutInds.find(name) == newLayoutInds.end()) {
    // i don't know if works for negative offsets too
    //assert(isRelative && offset >= 0);

    // this is a class we don't have any metadata about (i.e. there is no child of its
    // that has a defined vtable). We assume this should never get called in a
    // statically linked binary.
    assert(!knowsAbout(name) || !hasDefinedChild(name));
    return offset;
  }

  return oldIndexToNew2(name, offset, isRelative);
}

int64_t SDModule::getCloudSize(const SDModule::vtbl_name_t& vtbl) {
  return cloudSizeMap[vtbl];
}

llvm::Constant* SDModule::getVTableRangeStart(const SDModule::vtbl_t& vtbl) {
  return newVTableStartAddrMap[vtbl];
}

llvm::Constant* SDModule::getVTableRangeEnd(const SDModule::vtbl_t& vtbl, const SDModule::vtbl_name_t &rootName) {
  llvm::Constant* start = newVTableStartAddrMap[vtbl];
  int64_t size = getCloudSize(rootName);
  return llvm::ConstantExpr::getAdd(start, llvm::ConstantInt::get(start->getType(), size * WORD_WIDTH));
}

uint32_t SDModule::calculateChildrenCounts(const SDModule::vtbl_t& root){
  uint32_t count = isDefined(root) ? 1 : 0;
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
//      assert(sd_isVthunk(f->getName()));
      if(! sd_isVthunk(f->getName())) {
        f->dump();
        assert(false);
      }
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


SDModule::vtbl_t SDModule::getFirstDefinedChild(const vtbl_t &vtbl) {
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

bool SDModule::hasDefinedChild(const vtbl_t &vtbl) {
  order_t const &order = preorder(vtbl);

  for (const vtbl_t& c : order) {
    if (isDefined(c))
      return true;
  }

  return false;
}

bool SDModule::knowsAbout(const vtbl_t &vtbl) {
  return cloudMap.find(vtbl) != cloudMap.end();
}

void SDChangeIndices::handleSDGetVtblIndex(Module* M) {
  Function *sd_vtbl_indexF =
      M->getFunction(Intrinsic::getName(Intrinsic::sd_get_vtbl_index));

  // if the function doesn't exist, do nothing
  if (!sd_vtbl_indexF)
    return;

  llvm::LLVMContext& C = M->getContext();
  Type* intType = IntegerType::getInt64Ty(C);

  // for each use of the function
  for (const Use &U : sd_vtbl_indexF->uses()) {
    // get the call inst
    llvm::CallInst* CI = cast<CallInst>(U.getUser());
    // get the arguments
    llvm::ConstantInt* arg1 = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    assert(arg1);
    llvm::MetadataAsValue* arg2 = dyn_cast<MetadataAsValue>(CI->getArgOperand(1));
    assert(arg2);
    MDNode* mdNode = dyn_cast<MDNode>(arg2->getMetadata());
    assert(mdNode);

    // first argument is the old vtable index
    int64_t oldIndex = arg1->getSExtValue();

    // second one is the tuple that contains the class name and the corresponding global var.
    // note that the global variable isn't always emitted
    std::string className = sd_getClassNameFromMD(mdNode,0);

    // calculate the new index
    int64_t newIndex = sdModule->oldIndexToNew(className,oldIndex, true);

    // convert the integer to llvm value
    llvm::Value* newConsIntInd = llvm::ConstantInt::get(intType, newIndex);

    // since the result of the call instruction is i64, replace all of its occurence with this one
    IRBuilder<> B(CI);
    llvm::Value *Args[] = {newConsIntInd};
    llvm::Value* newIntr = B.CreateCall(Intrinsic::getDeclaration(M,
          Intrinsic::sd_subst_vtbl_index),
          Args);

    CI->replaceAllUsesWith(newIntr);
    CI->eraseFromParent();
  }
}

#include <iostream>

void SDChangeIndices::handleSDCheckVtbl(Module* M) {
  Function *sd_vtbl_indexF =
      M->getFunction(Intrinsic::getName(Intrinsic::sd_check_vtbl));
  const DataLayout &DL = M->getDataLayout();
  llvm::LLVMContext& C = M->getContext();
  Type *IntPtrTy = DL.getIntPtrType(C, 0);

  // if the function doesn't exist, do nothing
  if (!sd_vtbl_indexF)
    return;

  uint64_t total_targets = 0;
  uint64_t check_count = 0;

  // for each use of the function
  for (const Use &U : sd_vtbl_indexF->uses()) {
    // get the call inst
    llvm::CallInst* CI = cast<CallInst>(U.getUser());
    // get the arguments
    llvm::Value* vptr = CI->getArgOperand(0);
    assert(vptr);
    llvm::MetadataAsValue* arg2 = dyn_cast<MetadataAsValue>(CI->getArgOperand(1));
    assert(arg2);
    MDNode* mdNode = dyn_cast<MDNode>(arg2->getMetadata());
    assert(mdNode);

    // second one is the tuple that contains the class name and the corresponding global var.
    // note that the global variable isn't always emitted
    std::string className = sd_getClassNameFromMD(mdNode,0);
    SDModule::vtbl_t vtbl(className, 0);
    llvm::Constant *start;
    int64_t rangeWidth;

    if (sdModule->knowsAbout(vtbl) && sdModule->hasDefinedChild(vtbl)) {
      // calculate the new index
      start = sdModule->isUndefined(vtbl) ?
        sdModule->getVTableRangeStart(sdModule->getFirstDefinedChild(vtbl)) :
        sdModule->getVTableRangeStart(vtbl);
      rangeWidth = sdModule->getCloudSize(vtbl.first);
      total_targets += rangeWidth;
      check_count++;
    } else {
      // This is a class we have no metadata about (i.e. doesn't have any
      // non-virtuall subclasses) or has been elimintated by DCE. In a fully statically linked binary we
      // should never be able to create an instance of this.
      start = NULL;
      rangeWidth = 0;
    }
    LLVMContext& C = CI->getContext();

    if (start) {
      IRBuilder<> builder(CI);
      builder.SetInsertPoint(CI);

      {
        // The shift here is implicit since rangeWidth is in terms of indices, not bytes
        llvm::Value *width = llvm::ConstantInt::get(IntPtrTy, rangeWidth);
        llvm::Type *Int8PtrTy = IntegerType::getInt8PtrTy(C);
        llvm::Value *castVptr = builder.CreateBitCast(vptr, Int8PtrTy);
        llvm::Constant* eight = llvm::ConstantInt::get(IntPtrTy, 8);
        llvm::Value *Args[] = {castVptr, start, width, eight};
        llvm::Value* newIntr = builder.CreateCall(Intrinsic::getDeclaration(M,
              Intrinsic::sd_subst_check_range),
              Args);

        CI->replaceAllUsesWith(newIntr);
        CI->eraseFromParent();
        if (rangeWidth == 1)
          nEq ++;
        else
          nRange ++;
      }
    } else {
      CI->replaceAllUsesWith(llvm::ConstantInt::getFalse(C));
      CI->eraseFromParent();
      nConstFalse ++;
    }
  }

  if(check_count){
    sd_print("avg # targets : (%lu) %g\n", total_targets, total_targets * 1.0 / check_count);
  } else {
    sd_print("avg # targets : no vtbl check\n");
  }
}

void SDChangeIndices::handleSDGetCheckedVPtr(Module* M) {
  Function *sd_get_vptrF =
      M->getFunction(Intrinsic::getName(Intrinsic::sd_get_checked_vptr));
  const DataLayout &DL = M->getDataLayout();
  llvm::LLVMContext& C = M->getContext();
  Type *IntPtrTy = DL.getIntPtrType(C, 0);

  // if the function doesn't exist, do nothing
  if (!sd_get_vptrF)
    return;

  std::vector<llvm::CallInst*> uses;

  for (const Use &U : sd_get_vptrF->uses()) {
    uses.push_back(cast<CallInst>(U.getUser()));
  }

  // for each use of the function
  for (auto CI : uses) {
    llvm::Function* curFn = CI->getParent()->getParent();
    // get the arguments
    llvm::Value* vptr = CI->getArgOperand(0);
    assert(vptr);
    llvm::MetadataAsValue* arg2 = dyn_cast<MetadataAsValue>(CI->getArgOperand(1));
    assert(arg2);
    MDNode* mdNode = dyn_cast<MDNode>(arg2->getMetadata());
    assert(mdNode);

    // second one is the tuple that contains the class name and the corresponding global var.
    // note that the global variable isn't always emitted
    std::string className = sd_getClassNameFromMD(mdNode,0);
    SDModule::vtbl_t vtbl(className, 0);
    llvm::Constant *start, *end;
    int64_t rangeWidth;

    if (sdModule->knowsAbout(vtbl) && sdModule->hasDefinedChild(vtbl)) {
      // calculate the new index
      SDModule::vtbl_t definedRepr = sdModule->isUndefined(vtbl) ?
        sdModule->getFirstDefinedChild(vtbl) : vtbl;

      start = sdModule->getVTableRangeStart(definedRepr);
      end = sdModule->getVTableRangeEnd(definedRepr, vtbl.first);
      rangeWidth = sdModule->getCloudSize(vtbl.first);
    } else {
      // This is a class we have no metadata about (i.e. doesn't have any
      // non-virtuall subclasses) or has been elimintated by DCE. In a fully statically linked binary we
      // should never be able to create an instance of this.
      start = NULL;
      end = NULL;
      rangeWidth = 0;
    }
    LLVMContext& C = CI->getContext();

    if (start) {

      if (sdModule->validConstVptr(className, DL, vptr, 0)) {
        // Can statically discharge this check
        CI->replaceAllUsesWith(vptr);
        CI->eraseFromParent();
        nConstTrue++;
      } else {
        BasicBlock* oldBB = CI->getParent();
        BasicBlock* successBB = oldBB->splitBasicBlock(CI, "vtblCheck.success");
        BasicBlock* trapBB = BasicBlock::Create(C, "", curFn, successBB);

        // After splitting the old block ends in an unconditional jump
        llvm::Instruction* oldBBTerm = oldBB->getTerminator();
        
        if (rangeWidth == 1) {
          // Emit single cmp
          IRBuilder<> builder(oldBBTerm);
          builder.SetInsertPoint(oldBBTerm);
          llvm::Value* vptrInt = builder.CreatePtrToInt(vptr, start->getType());

          Value *isEq = builder.CreateICmpNE(start, vptrInt);
          builder.CreateCondBr(isEq, trapBB, successBB);
          nEq ++;
        } else {
          // Emit the range check
          BasicBlock *isOverMinBB = BasicBlock::Create(C, "vtblCheck.isOverMin", curFn, successBB);
          IRBuilder<> builder(oldBBTerm);
          builder.SetInsertPoint(oldBBTerm);

          llvm::Value* vptrInt = builder.CreatePtrToInt(vptr, start->getType());
          Value *isOverMin = builder.CreateICmpUGT(start, vptrInt);
          builder.CreateCondBr(isOverMin, trapBB, isOverMinBB);
          builder.SetInsertPoint(isOverMinBB);
          Value *isUnderMax = builder.CreateICmpULT(end, vptrInt);
          builder.CreateCondBr(isUnderMax, trapBB, successBB);
          nRange ++;
        }

        oldBBTerm->eraseFromParent();
        CI->replaceAllUsesWith(vptr);
        CI->eraseFromParent();

        IRBuilder<> trapBuilder(trapBB);
        trapBuilder.CreateCall(Intrinsic::getDeclaration(M, llvm::Intrinsic::trap));
        trapBuilder.CreateUnreachable();
      }
    } else {
      IRBuilder<> builder(CI);
      builder.CreateCall(Intrinsic::getDeclaration(M, llvm::Intrinsic::trap));
      builder.CreateUnreachable();
      CI->replaceAllUsesWith(llvm::ConstantPointerNull::get(IntegerType::getInt8PtrTy(C)));
      CI->eraseFromParent();
      nConstFalse ++;
    }
  }
}

void SDChangeIndices::handleRemainingSDGetVcallIndex(Module* M) {
  Function *sd_vcall_indexF =
      M->getFunction(Intrinsic::getName(Intrinsic::sd_get_vcall_index));

  // if the function doesn't exist, do nothing
  if (!sd_vcall_indexF)
    return;

  // for each use of the function
  for (const Use &U : sd_vcall_indexF->uses()) {
    // get the call inst
    llvm::CallInst* CI = cast<CallInst>(U.getUser());
    // get the arguments
    llvm::ConstantInt* arg1 = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    assert(arg1);

    // since the result of the call instruction is i64, replace all of its occurence with this one
    CI->replaceAllUsesWith(arg1);
  }
}
