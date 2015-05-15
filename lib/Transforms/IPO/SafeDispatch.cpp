#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

#include <list>
#include <vector>
#include <set>
#include <map>

// you have to modify the following files for each additional LLVM pass
// 1. IPO.h and IPO.cpp
// 2. LinkAllPasses.h
// 3. InitializePasses.h

using namespace llvm;

#define DEBUG_TYPE "cc"

namespace {
  /**
   * More fine grain basic block pass for the SafeDispatch Gold Plugin
   */
  struct ChangeConstant : public BasicBlockPass {
    static char ID; // Pass identification, replacement for typeid

    ChangeConstant() : BasicBlockPass(ID) {
      initializeChangeConstantPass(*PassRegistry::getPassRegistry());
    }

    bool runOnBasicBlock(BasicBlock &BB) override {
      Module* module = BB.getParent()->getParent();

      unsigned classNameMDId = module->getMDKindID(SD_MD_CLASS_NAME);
      unsigned castFromMDId = module->getMDKindID(SD_MD_CAST_FROM);
      unsigned typeidMDId = module->getMDKindID(SD_MD_TYPEID);
      unsigned vcallMDId = module->getMDKindID(SD_MD_VCALL);
      unsigned vbaseMDId = module->getMDKindID(SD_MD_VBASE);
      unsigned memptrMDId = module->getMDKindID(SD_MD_MEMPTR);
      unsigned memptr2MDId = module->getMDKindID(SD_MD_MEMPTR2);
      unsigned memptrOptMdId = module->getMDKindID(SD_MD_MEMPTR_OPT);

      llvm::MDNode* vfptrMDNode = NULL;
      llvm::MDNode* vcallMDNode = NULL;
      llvm::MDNode* vbaseMDNode = NULL;
      llvm::MDNode* memptrMDNode = NULL;
      llvm::MDNode* memptr2MDNode = NULL;
      llvm::MDNode* memptrOptMDNode = NULL;

      std::vector<Instruction*> instructions;
      for(BasicBlock::iterator instItr = BB.begin(); instItr != BB.end(); instItr++) {
        instructions.push_back(instItr);
      }

      for(std::vector<Instruction*>::iterator instItr = instructions.begin();
          instItr != instructions.end(); instItr++) {
        Instruction* inst = *instItr;
        unsigned opcode = inst->getOpcode();

        // gep instruction
        if (opcode == GEP_OPCODE) {
          GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(inst);
          assert(gepInst);

          if((vfptrMDNode = inst->getMetadata(classNameMDId))){
            multVfptrIndexBy2(vfptrMDNode, gepInst);

          } else if ((vbaseMDNode = inst->getMetadata(vbaseMDId))) {
            int64_t oldValue = getMetadataConstant(vbaseMDNode, 1);
            sd_changeGEPIndex(gepInst, 1, oldValue * 2);

          } else if ((memptrOptMDNode = inst->getMetadata(memptrOptMdId))) {
            ConstantInt* ci = dyn_cast<ConstantInt>(inst->getOperand(1));
            if (ci) {
              // this happens when program is compiled with -O
              // vtable index of the member pointer is put directly into the
              // GEP instruction using constant folding

              int64_t oldValue = ci->getSExtValue();
              sd_changeGEPIndex(gepInst, 1, oldValue * 2);
            }
          }
        }

        // call instruction
        else if (inst->getMetadata(castFromMDId)) {
          replaceDynamicCast(module, inst);
        }

        // load instruction
        else if (inst->getMetadata(typeidMDId)) {
          multRTTIOffsetBy2(inst);
        }

        // bitcast instruction
        else if ((vcallMDNode = inst->getMetadata(vcallMDId))) {
          int64_t oldValue = getMetadataConstant(vcallMDNode, 1);
          multVcallOffsetBy2(inst, oldValue);
        }

        // call instruction
        else if ((memptrMDNode = inst->getMetadata(memptrMDId))) {
          handleStoreMemberPointer(memptrMDNode, inst);
        }

        // select instruction
        else if (opcode == SELECT_OPCODE && (memptr2MDNode = inst->getMetadata(memptr2MDId))) {
          handleSelectMemberPointer(memptr2MDNode, inst);
        }
      }
      return true;
    }

  private:

    void multVfptrIndexBy2(llvm::MDNode* mdNode, Instruction* inst) {
      GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(inst);
      assert(gepInst);

      StringRef className = cast<llvm::MDString>(mdNode->getOperand(0))->getString();
      assert(sd_isVtableName_ref(className));

      ConstantInt* index = cast<ConstantInt>(gepInst->getOperand(1));
      uint64_t indexVal = *(index->getValue().getRawData());
      uint64_t newIndexVal = indexVal * 2;

      gepInst->setOperand(1, ConstantInt::get(IntegerType::getInt64Ty(gepInst->getContext()),
                          newIndexVal, false));
    }

    void replaceDynamicCast(Module* module, Instruction* inst) {
      Function* from = module->getFunction("__dynamic_cast");

      if (!from)
        return;

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
      arguments.push_back(ConstantInt::get(context, APInt(64, -1 * 2 * 8, true))); // rtti
      arguments.push_back(ConstantInt::get(context, APInt(64, -2 * 2 * 8, true))); // ott

      bool isReplaced = sd_replaceCallFunctionWith(callInst, dyncastFunF, arguments);
      assert(isReplaced);
    }

    void multRTTIOffsetBy2(Instruction* inst) {
      LoadInst* loadInst = dyn_cast<LoadInst>(inst);
      assert(loadInst);
      GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(loadInst->getOperand(0));
      assert(gepInst);

      sd_changeGEPIndex(gepInst, 1, -2UL);
    }

    void multVcallOffsetBy2(Instruction* inst, int64_t oldValue) {
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

      sd_changeGEPIndex(gepInst2, 1, oldValue * 2);
    }

    void replaceConstantStruct(ConstantStruct* CS, Instruction* inst) {
      std::vector<Constant*> V;
      ConstantInt* ci = dyn_cast<ConstantInt>(CS->getOperand(0));
      assert(ci);

      V.push_back(ConstantInt::get(
          Type::getInt64Ty(inst->getContext()),
          ci->getSExtValue() * 2 - 1));

      ci = dyn_cast<ConstantInt>(CS->getOperand(1));
      assert(ci);

      V.push_back(ConstantInt::get(
          Type::getInt64Ty(inst->getContext()),
          ci->getSExtValue()));

      Constant* CSNew = ConstantStruct::getAnon(V);
      assert(CSNew);

      inst->replaceUsesOfWith(CS,CSNew);
    }

    void handleStoreMemberPointer(llvm::MDNode* mdNode, Instruction* inst){
      std::string className = cast<llvm::MDString>(mdNode->getOperand(0))->getString();

      StoreInst* storeInst = dyn_cast<StoreInst>(inst);
      assert(storeInst);

      ConstantStruct* CS = dyn_cast<ConstantStruct>(storeInst->getOperand(0));
      assert(CS);
      replaceConstantStruct(CS, storeInst);
    }

    void handleSelectMemberPointer(llvm::MDNode* mdNode, Instruction* inst){
      std::string className1 = cast<llvm::MDString>(mdNode->getOperand(0))->getString();
      std::string className2 = cast<llvm::MDString>(mdNode->getOperand(1))->getString();

      SelectInst* selectInst = dyn_cast<SelectInst>(inst);
      assert(selectInst);

      ConstantStruct* CS1 = dyn_cast<ConstantStruct>(selectInst->getOperand(1));
      assert(CS1);
      replaceConstantStruct(CS1, selectInst);

      ConstantStruct* CS2 = dyn_cast<ConstantStruct>(selectInst->getOperand(2));
      assert(CS2);
      replaceConstantStruct(CS2, selectInst);
    }

    int64_t getMetadataConstant(llvm::MDNode* mdNode, unsigned operandNo) {
      llvm::MDTuple* mdTuple = dyn_cast<llvm::MDTuple>(mdNode);
      assert(mdTuple);

      llvm::ConstantAsMetadata* constantMD = dyn_cast<ConstantAsMetadata>(
            mdTuple->getOperand(operandNo));
      assert(constantMD);

      ConstantInt* constantInt = dyn_cast<ConstantInt>(constantMD->getValue());
      assert(constantInt);

      return constantInt->getSExtValue();
    }


    FunctionType* getDynCastFunType(LLVMContext& context) {
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

    void duplicatePrintf(Instruction* inst) {
      CallInst* callInst = dyn_cast<CallInst>(inst);
      assert(callInst);
      Function* calledF = callInst->getCalledFunction();

      if (calledF && calledF->getName() == "printf") {
        IRBuilder<> builder(callInst);
        builder.SetInsertPoint(callInst);

        std::vector<Value*> arguments;
        for (unsigned argInd = 0; argInd < callInst->getNumArgOperands(); ++argInd) {
          arguments.push_back(callInst->getArgOperand(argInd));
        }

        builder.CreateCall(calledF, arguments, "sd.call");
        sd_print("Duplicated printf call");
      }
    }

    void change42to43(Instruction* inst) {
      StoreInst* storeInst = dyn_cast<StoreInst>(inst);
      assert(storeInst);

      Value* storeVal = storeInst->getOperand(0);
      ConstantInt* constIntVal = dyn_cast<ConstantInt>(storeVal);

      if(constIntVal) {
        if (*(constIntVal->getValue().getRawData()) == 42) {
          IntegerType* intType = constIntVal->getType();
          inst->setOperand(0, ConstantInt::get(intType, 43, false));
          sd_print("changed constant val 42 to 43\n");
        }
      }
    }
  };

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
      std::cerr << "Deleted SDModule object" << std::endl;
    }

    // variable definitions

    typedef std::string                                  vtbl_name_t;
    typedef std::pair<vtbl_name_t, uint64_t>             vtbl_t;
    typedef std::map<vtbl_t, std::set<vtbl_t>>           cloud_map_t;
    typedef std::set<vtbl_name_t>                        roots_t;
    typedef std::map<vtbl_name_t, std::vector<uint64_t>> addrpt_map_t;
    typedef std::pair<uint64_t, uint64_t>                range_t;
    typedef std::map<vtbl_name_t, std::vector<range_t>>  range_map_t;
    typedef std::map<vtbl_t, vtbl_name_t>                ancestor_map_t;
    typedef std::map<vtbl_t, std::vector<uint64_t>>      new_layout_inds_t;
    typedef std::pair<vtbl_t, uint64_t>       					 interleaving_t;
    typedef std::list<interleaving_t>                    interleaving_list_t;
    typedef std::map<vtbl_name_t, interleaving_list_t>   interleaving_map_t;
    typedef std::vector<vtbl_t>                          order_t;

    cloud_map_t cloudMap;                              // (vtbl,ind) -> set<(vtbl,ind)>
    roots_t roots;                                     // set<vtbl>
    addrpt_map_t addrPtMap;                            // vtbl -> [addr pt]
    range_map_t rangeMap;                              // vtbl -> [(start,end)]
    ancestor_map_t ancestorMap;                        // (vtbl,ind) -> root vtbl (is this necessary?)
    new_layout_inds_t newLayoutInds;                   // (vtbl,ind) -> [new ind inside interleaved vtbl]
    interleaving_map_t interleavingMap;                // root -> new layouts map
    std::map<vtbl_name_t, ConstantArray*> oldVTables;  // vtbl -> &[vtable element]

    // these should match the structs defined at SafeDispatchVtblMD.h
    struct nmd_sub_t {
      uint64_t order;
      vtbl_name_t parentName;
      uint64_t start;
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

      buildClouds(M);      // part 1
      interleaveClouds(M); // part 2

      return true;
    }

  private:
    /**
     * Reads the NamedMDNodes in the given module and creates the class hierarchy
     */
    void buildClouds(Module &M);

    /**
     * Interleave the generated clouds and create a new global variable for each of them.
     */
    void interleaveClouds(Module &M) {
      for (roots_t::iterator itr = roots.begin(); itr != roots.end(); itr++) {
        vtbl_name_t vtbl = *itr;

        interleaveCloud(vtbl);
        calculateNewLayoutInds(vtbl);
        createNewVTable(M, vtbl);
      }

      // remove the old vtables
      for (auto itr : oldVTables) {
        GlobalVariable* var = M.getGlobalVariable(itr.first, true);
        assert(var && var->use_empty());
        var->eraseFromParent();
      }
    }

    /**
     * Extract the vtable info from the metadata and put it into a struct
     */
    nmd_t extractMetadata(NamedMDNode* md);

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
     * Return a list that contains the preorder traversal of the tree
     * starting from the given node
     */
    order_t preorder(vtbl_t& root);
    void preorderHelper(order_t& nodes, const vtbl_t& root);

    /**
     * This method is used for filling the both (negative and positive) parts of an
     * interleaved vtable of a cloud.
     *
     * @param part        : A list reference to record the <vtbl_t, element index> pairs
     * @param order       : A list that contains the preorder traversal
     * @param positiveOff : true if we're filling the positive (function pointers) part
     */
    void fillVtablePart(interleaving_list_t& part, const order_t& order, bool positiveOff);

    void printVtable(GlobalVariable* globalVar);
  };
}

char ChangeConstant::ID = 0;
char SDModule::ID = 0;

INITIALIZE_PASS(ChangeConstant, "cc", "Change Constant", false, false)
INITIALIZE_PASS(SDModule, "sdmp", "Module pass for SafeDispatch", false, false)

BasicBlockPass* llvm::createChangeConstantPass() {
  return new ChangeConstant();
}

ModulePass* llvm::createSDModulePass() {
  return new SDModule();
}

/// ----------------------------------------------------------------------------
/// Analysis implementation
/// ----------------------------------------------------------------------------

void SDModule::interleaveCloud(SDModule::vtbl_name_t& vtbl) {
  assert(roots.find(vtbl) != roots.end());

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
  assert(interleavingMap.find(vtbl) != interleavingMap.end());

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
  PointerType* vtblElemType = PointerType::get(IntegerType::get(M.getContext(), 8), 0);
  ArrayType* newArrType = ArrayType::get(vtblElemType, newSize);

  // fill the interleaved vtable element list
  std::vector<Constant*> newVtableElems;
  for (const interleaving_t& ivtbl : newVtbl) {
    ConstantArray* vtable = oldVTables[ivtbl.first.first];
    newVtableElems.push_back(vtable->getOperand(ivtbl.second));
  }

  // create the constant initializer
  Constant* newVtableInit = ConstantArray::get(newArrType, newVtableElems);

  // create the global variable
  GlobalVariable* newVtable = new GlobalVariable(M, newArrType, true,
                                                 GlobalVariable::ExternalLinkage,
                                                 nullptr, "_SD" + vtbl);
  newVtable->setAlignment(8);
  newVtable->setInitializer(newVtableInit);

  // to start changing the original uses of the vtables, first get all the classes in the cloud
  order_t cloud;
  vtbl_t root(vtbl,0);
  preorderHelper(cloud, root);

  Constant* zero = ConstantInt::get(M.getContext(), APInt(64, 0));
  for (const vtbl_t& v : cloud) {
    // find the original vtable
    GlobalVariable* globalVar = M.getGlobalVariable(v.first, true);
    assert(globalVar);

    // replace the uses of the original vtables
    for (GlobalVariable::user_iterator userItr = globalVar->user_begin();
         userItr != globalVar->user_end(); userItr++) {
      // this should be a getelementptr
      User* user = *userItr;
      ConstantExpr* userCE = dyn_cast<ConstantExpr>(user);
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

      sd_print("changed vtbl use: %s %ld ==> %s %ld\n",
               v.first.c_str(), oldAddrPt, vtbl.c_str(), newAddrPt);
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

    // TODO (rkici) : add a check to make sure that the interleaved functions are OK

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
  ConstantAsMetadata* cam = dyn_cast<ConstantAsMetadata>(op.get());
  assert(cam);
  ConstantInt* ci = dyn_cast<ConstantInt>(cam->getValue());
  assert(ci);

  return ci->getSExtValue();
}

static inline SDModule::vtbl_name_t
sd_getStringFromMDTuple(const MDOperand& op) {
  MDString* mds = dyn_cast<MDString>(op.get());
  assert(mds);

  return mds->getString().str();
}

void SDModule::buildClouds(Module &M) {
  for(auto itr = M.getNamedMDList().begin(); itr != M.getNamedMDList().end(); itr++) {
    NamedMDNode* md = itr;

    // check if this is a metadata that we've added
    if(! md->getName().startswith(SD_MD_CLASSINFO))
      continue;

    nmd_t info = extractMetadata(md);

    // record the old vtable array
    GlobalVariable* oldVtable = M.getGlobalVariable(info.className, true);
    assert(oldVtable);
    ConstantArray* vtable = dyn_cast<ConstantArray>(oldVtable->getInitializer());
    assert(vtable);
    oldVTables[info.className] = vtable;

    for(unsigned ind = 0; ind < info.subVTables.size(); ind++) {
      nmd_sub_t* subInfo = & info.subVTables[ind];
      vtbl_t name(info.className, ind);

      if (subInfo->parentName != "") {
        // add the current class to the parent's children set
        cloudMap[vtbl_t(subInfo->parentName,0)].insert(name);
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
    }
  }
}

SDModule::nmd_t SDModule::extractMetadata(NamedMDNode* md) {
  SDModule::nmd_t info;
  info.className = (cast<MDString>(md->getOperand(0)->getOperand(0)))->getString().str();

  sd_print("%s:\n", info.className.c_str());

  for (unsigned i = 1; i < md->getNumOperands(); ++i) {
    SDModule::nmd_sub_t subInfo;
    llvm::MDTuple* tup = dyn_cast<llvm::MDTuple>(md->getOperand(i));
    assert(tup && tup->getNumOperands() == 5);

    subInfo.order = sd_getNumberFromMDTuple(tup->getOperand(0));
    subInfo.parentName = sd_getStringFromMDTuple(tup->getOperand(1));

    subInfo.start = sd_getNumberFromMDTuple(tup->getOperand(2));
    subInfo.end = sd_getNumberFromMDTuple(tup->getOperand(3));
    subInfo.addressPoint = sd_getNumberFromMDTuple(tup->getOperand(4));

    sd_print("%lu: %s[%lu,%lu] %lu\n",subInfo.order,
             (subInfo.parentName.size() > 0 ? (subInfo.parentName + " ").c_str() : ""),
             subInfo.start, subInfo.end, subInfo.addressPoint);

    info.subVTables.push_back(subInfo);
  }

  return info;
}

/// ----------------------------------------------------------------------------
/// Helper functions
/// ----------------------------------------------------------------------------

void SDModule::printVtable(GlobalVariable* globalVar) {
  StringRef varName = globalVar->getName();
  ConstantArray* vtable = dyn_cast<ConstantArray>(globalVar->getInitializer());
  assert(vtable);

  ConstantExpr* ce = NULL;
  ConstantInt* vtblInt = NULL;
  unsigned opcode = 0;

  sd_print("%s elements:\n", varName.bytes_begin());
  for (unsigned vtblInd = 0; vtblInd < vtable->getNumOperands(); vtblInd++) {
    ce = dyn_cast<ConstantExpr>(vtable->getOperand(vtblInd));
    opcode = ce ? ce->getOpcode() : 0;

    switch (opcode) {
      case BITCAST_OPCODE:
        sd_print("%-2u %s\n", vtblInd, ce->getOperand(0)->getName().bytes_begin());
        break;
      case INTTOPTR_OPCODE:
        vtblInt = dyn_cast<ConstantInt>(ce->getOperand(0));
        assert(vtblInt);
        sd_print("%-2u %ld\n", vtblInd, vtblInt->getSExtValue());
        break;
      default: // this must be a null value
        sd_print("%-2u 0\n", vtblInd);
        break;
    }
  }
}


bool llvm::sd_replaceCallFunctionWith(CallInst* callInst, Function* to, std::vector<Value*> args) {
  assert(callInst && to && args.size() > 0);

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

// lower_bound with start addresses
