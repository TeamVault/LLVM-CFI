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
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/MDBuilder.h"

#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>
#include <vector>
#include <set>
#include <map>
#include <math.h>
#include <algorithm>
#include <iostream>
#include <limits>

// you have to modify the following 4 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManagerBuilder.cpp

using namespace llvm;

namespace {
  /**
   * Pass for updating the annotated instructions with the new indices
   Paul: the UpdateIndices pass just runs the runOnModule() function
   all the function called in this module reside in the next pass, Subst_Module.
   This pass adds the needed checks.
   */
  struct SDUpdateIndices : public ModulePass {
    static char ID; // Pass identification, replacement for typeid

    SDUpdateIndices() : ModulePass(ID) {
      sd_print("initializing SDUpdateIndices pass\n");
      initializeSDUpdateIndicesPass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDUpdateIndices() {
      sd_print("deleting SDUpdateIndices pass\n");
    }

    bool runOnModule(Module &M) override {
      //Paul: first get the results from the previous layout builder pass 
      layoutBuilder = &getAnalysis<SDLayoutBuilder>();
      assert(layoutBuilder);

      //Paul: second get the results from the class hierarchy analysis pass
      cha = &getAnalysis<SDBuildCHA>();

      sd_print("P4. Started running the 4th pass (Update indices) ...\n");

      //Paul: substitute the old v table index witht the new one
      //Intrinsic::sd_get_vtbl_index -> Intrinsic::sd_subst_vtbl_index
      handleSDGetVtblIndex(&M); 
 
      //Paul: adds the range check (casted_vptr, start, width, alingment)
      //Intrinsic::sd_check_vtbl -> Intrinsic::sd_subst_check_range
      handleSDCheckVtbl(&M);  

      //Paul: add the range checks, success, failed path, the trap and replace the terminator   
      //Intrinsic::sd_get_checked_vptr ->  Intrinsic::sd_subst_check_range             
      handleSDGetCheckedVtbl(&M);            

      //Paul: this are for the additional v pointer which are not checked based on ranges 
      //Intrinsic::sd_get_vcall_index -> null (there is no substitution function used here)
      handleRemainingSDGetVcallIndex(&M);    

      layoutBuilder->removeOldLayouts(M);    //Paul: remove old layouts
      layoutBuilder->clearAnalysisResults(); //Paul: clear all data structures holding analysis data

      sd_print("P4. Finished removing thunks from (Update indices) pass...\n");
      return true;
    }

    /*Paul: 
    this method is used to get analysis results on which this pass depends*/
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<SDLayoutBuilder>(); //Paul: depends on layout builder pass
      AU.addRequired<SDBuildCHA>(); //Paul: depends on CHA pass
      AU.addPreserved<SDBuildCHA>(); //Paul: should preserve the information from the CHA pass
    }

  private:
    SDLayoutBuilder* layoutBuilder;
    SDBuildCHA* cha;
    
    // metadata ids
    void handleSDGetVtblIndex(Module* M);
    void handleSDCheckVtbl(Module* M);
    void handleSDGetCheckedVtbl(Module* M);
    void handleRemainingSDGetVcallIndex(Module* M);
  };
}

/// ----------------------------------------------------------------------------
/// SDUpdateIndices implementation, this are executed inside P4. Next, P5 is executed.
/// ----------------------------------------------------------------------------

static std::string sd_getClassNameFromMD(llvm::MDNode* mdNode, unsigned operandNo = 0) {
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

//Paul: this returns the v table index and puts it in a function 
// it uses this functions to get the old v table index and to substitute it 
//Intrinsic::sd_get_vtbl_index -> Intrinsic::sd_subst_vtbl_index
void SDUpdateIndices::handleSDGetVtblIndex(Module* M) {
  Function *sd_vtbl_indexF = M->getFunction(Intrinsic::getName(Intrinsic::sd_get_vtbl_index));

  // if the function doesn't exist, do nothing
  if (!sd_vtbl_indexF)
    return;

  llvm::LLVMContext& C = M->getContext();
  Type* intType = IntegerType::getInt64Ty(C);

  // for each use of the function
  for (const Use &U : sd_vtbl_indexF->uses()) {
    
    // get the call inst
    llvm::CallInst* CI = cast<CallInst>(U.getUser());

    // get the old arguments
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
    SDLayoutBuilder::vtbl_t classVtbl(className, 0);

    // calculate the new index
    int64_t newIndex = layoutBuilder->translateVtblInd(classVtbl, oldIndex, true);

    // convert the integer to llvm value
    llvm::Value* newConsIntInd = llvm::ConstantInt::get(intType, newIndex);

    // since the result of the call instruction is i64, replace all of its occurence with this one
    IRBuilder<> B(CI);
    llvm::Value *Args[] = {newConsIntInd};
    llvm::Value* newIntr = B.CreateCall(Intrinsic::getDeclaration(M,
          Intrinsic::sd_subst_vtbl_index),
          Args);

    CI->replaceAllUsesWith(newIntr); //Paul: replace the old v table index with the new one 
    CI->eraseFromParent();
  }
}

//Paul: this is used for the in-place sort operation 
struct range_less_than_key {
  inline bool operator()(const SDLayoutBuilder::mem_range_t &r1, const SDLayoutBuilder::mem_range_t &r2) {
    return r1.second > r2.second; // Invert sign to sort in descending order
  }
};

//Paul: adds the range check (casted_vptr, start, width, alingment)
//add check v table and check v table range 
// it uses: 
// Intrinsic::sd_check_vtbl -> Intrinsic::sd_subst_check_range
void SDUpdateIndices::handleSDCheckVtbl(Module* M) {
  Function *sd_vtbl_indexF = M->getFunction(Intrinsic::getName(Intrinsic::sd_check_vtbl));
  const DataLayout &DL = M->getDataLayout();
  llvm::LLVMContext& C = M->getContext();
  Type *IntPtrTy = DL.getIntPtrType(C, 0);

  // if the function doesn't exist, do nothing
  if (!sd_vtbl_indexF)
    return;

  // for each use of the function
  for (const Use &U : sd_vtbl_indexF->uses()) {
    
    // get the call instruction from the uses 
    llvm::CallInst* CI = cast<CallInst>(U.getUser());

    //sd_print("CI: %s \n", CI->data());

    // get the arguments
    llvm::Value* vptr = CI->getArgOperand(0);
    assert(vptr); //Paul: this is the v pointer

    llvm::MetadataAsValue* arg2 = dyn_cast<MetadataAsValue>(CI->getArgOperand(1));
    assert(arg2);

    MDNode* mdNode = dyn_cast<MDNode>(arg2->getMetadata());
    assert(mdNode);

    llvm::MetadataAsValue* arg3 = dyn_cast<MetadataAsValue>(CI->getArgOperand(2));
    assert(arg3);

    MDNode* mdNode1 = dyn_cast<MDNode>(arg3->getMetadata());
    assert(mdNode1);

    // second one is the tuple that contains the class name and the corresponding global var.
    // note that the global variable isn't always emitted

    //class name of the calling object
    std::string className = sd_getClassNameFromMD(mdNode,0);

    //class name of the base class ?
    std::string preciseClassName = sd_getClassNameFromMD(mdNode1,0);

    //declare a new v table with order number 0
    SDLayoutBuilder::vtbl_t vtbl(className, 0);

    llvm::Constant *start; //Paul: range start
    int64_t rangeWidth;    //Paul: range width

    sd_print("Callsite for class %s cha->knowsAbout(%s, %d) = %d) ", className.c_str(),
                                                              vtbl.first.c_str(), 
                                                                     vtbl.second, 
                                                           cha->knowsAbout(vtbl));

    if (cha->knowsAbout(vtbl)) {
      if (preciseClassName != className) {
        sd_print("More precise class name = %s\n", preciseClassName.c_str());
        int64_t ind = cha->getSubVTableIndex(preciseClassName, className);
        sd_print("Index = %d \n", ind);
        if (ind != -1) {
          vtbl = SDLayoutBuilder::vtbl_t(preciseClassName, ind);
        }
      } 
    }

    //Paul: calculate the start address of the new v table
    if (cha->knowsAbout(vtbl) &&
       (!cha->isUndefined(vtbl) || cha->hasFirstDefinedChild(vtbl))) {
      // calculate the new index
      start = cha->isUndefined(vtbl) ?
        layoutBuilder->getVTableRangeStart(cha->getFirstDefinedChild(vtbl)) : //Paul: first child or first v table
        layoutBuilder->getVTableRangeStart(vtbl);
      
      //Paul: cloud size represents the range width
      // It basically counts the number of children in that cloud. 
      // The children have to belong to the inheritance path,
      // this is not checked here or enforced.
      rangeWidth = cha->getCloudSize(vtbl.first); //count the number of children in the tree 
      sd_print(" [rangeWidth = %d start = %p]  \n", rangeWidth, start);
    } else {
      // This is a class we have no metadata about (i.e. doesn't have any
      // non-virtuall subclasses). In a fully statically linked binary we
      // should never be able to create an instance of this.
      start = NULL;
      rangeWidth = 0;
      //std::cerr << "Emitting empty range for " << vtbl.first << "," << vtbl.second << "\n";
      sd_print(" [ no metadata available ] \n");
    }

    LLVMContext& C = CI->getContext();
    
    //Paul: the start varible is not NULL
    if (start) {
      IRBuilder<> builder(CI);
      builder.SetInsertPoint(CI);//Paul: used to specifi insertion points

      //std::cerr << "llvm.sd.callsite.range:" << rangeWidth << std::endl;
        
      // The shift here is implicit since rangeWidth is in terms of indices, not bytes
      llvm::Value *width = llvm::ConstantInt::get(IntPtrTy, rangeWidth); //rangeWidth is here 0
      llvm::Type *Int8PtrTy = IntegerType::getInt8PtrTy(C);
      llvm::Value *castVptr = builder.CreateBitCast(vptr, Int8PtrTy); //create bitcast operation here 

      if(!cha->hasAncestor(vtbl)) {
        sd_print("%s\n", vtbl.first.data());
        assert(false);
      }

      SDLayoutBuilder::vtbl_name_t root = cha->getAncestor(vtbl);
      assert(layoutBuilder->alignmentMap.count(root));

      llvm::Constant* alignment = llvm::ConstantInt::get(IntPtrTy, layoutBuilder->alignmentMap[root]);
      llvm::Value *Args[] = {castVptr, start, width, alignment};
      llvm::Value* newIntr = builder.CreateCall(Intrinsic::getDeclaration(M,
                                                Intrinsic::sd_subst_check_range),
                                                Args);

      CI->replaceAllUsesWith(newIntr);//Paul: add a new call instruction with rangeWidth = 0 
      CI->eraseFromParent();

      /*
      if (rangeWidth > 1) {
        // The shift here is implicit since rangeWidth is in terms of indices, not bytes
        llvm::Value *width = llvm::ConstantInt::get(IntPtrTy, rangeWidth);

        // Rotate right by 3 to push the lowest order bits into the higher order bits
        llvm::Value *vptrInt = builder.CreatePtrToInt(vptr, IntPtrTy);
        llvm::Value *diff = builder.CreateSub(vptrInt, startInt);
        llvm::Value *diffShr = builder.CreateLShr(diff, 3);
        llvm::Value *diffShl = builder.CreateShl(diff, DL.getPointerSizeInBits(0) - 3);
        llvm::Value *diffRor = builder.CreateOr(diffShr, diffShl);

        llvm::Value *inRange = builder.CreateICmpULE(diffRor, width);
          
        CI->replaceAllUsesWith(inRange);
        CI->eraseFromParent();
      } else {
        llvm::Value *startInt = start; //builder.CreatePtrToInt(start, IntegerType::getInt64Ty(C));
        llvm::Value *vptrInt = builder.CreatePtrToInt(vptr, IntPtrTy);
        llvm::Value *inRange = builder.CreateICmpEQ(vptrInt, startInt);

        CI->replaceAllUsesWith(inRange);
        CI->eraseFromParent();
      }        
      */
    } else { //Paul: if start == NULL
      std::cerr << "llvm.sd.callsite.false:" << vtbl.first << "," << vtbl.second 
        << std::endl;
      CI->replaceAllUsesWith(llvm::ConstantInt::getFalse(C));
      CI->eraseFromParent();
    }
  }
}

//Paul: add the range checks, success, failed path, the trap and replace the terminator 
//add checked v table pointer, add subst range and the trap if failed
//it uses:  
// Intrinsic::sd_get_checked_vptr ->  Intrinsic::sd_subst_check_range
void SDUpdateIndices::handleSDGetCheckedVtbl(Module* M) {
  Function *sd_vtbl_indexF = M->getFunction(Intrinsic::getName(Intrinsic::sd_get_checked_vptr));
  const DataLayout &DL = M->getDataLayout(); //Paul: get data layout 
  llvm::LLVMContext& C = M->getContext();    //Paul: get the context
  Type *IntPtrTy = DL.getIntPtrType(C, 0);   //Paul: get the Int pointer type

  // if the function doesn't exist, do nothing
  if (!sd_vtbl_indexF)
    return;

  // Paul: iterate through all function uses
  for (const Use &U : sd_vtbl_indexF->uses()) {
    
    // get each call instruction
    llvm::CallInst* CI = cast<CallInst>(U.getUser());

    // get the v ptr
    llvm::Value* vptr = CI->getArgOperand(0);
    assert(vptr);//assert not null
 
    //Paul: get second operand
    llvm::MetadataAsValue* arg2 = dyn_cast<MetadataAsValue>(CI->getArgOperand(1));
    assert(arg2);//assert not null

    //Paul: get the metadata of the second param
    MDNode* mdNode = dyn_cast<MDNode>(arg2->getMetadata());
    assert(mdNode);//assert not null
    
    //Paul: get the third parameter
    llvm::MetadataAsValue* arg3 = dyn_cast<MetadataAsValue>(CI->getArgOperand(2));
    assert(arg3);//assert not null

    //Paul: get the metadata of the third param 
    MDNode* mdNode1 = dyn_cast<MDNode>(arg3->getMetadata());
    assert(mdNode1);//assert not null

    // second one is the tuple that contains the class name and the corresponding global var.
    // note that the global variable isn't always emitted
    //get the class name class name from argument 1
    std::string className = sd_getClassNameFromMD(mdNode, 0);       

    //get a more precise class name from argument 2
    std::string preciseClassName = sd_getClassNameFromMD(mdNode1,0);
    SDLayoutBuilder::vtbl_t vtbl(className, 0);
    llvm::Constant *start;
    int64_t rangeWidth;

    sd_print("Callsite for %s cha->knowsAbout(%s, %d) = %d)\n", className.c_str(),
      vtbl.first.c_str(), vtbl.second, cha->knowsAbout(vtbl));
  
    //Paul: check if the class hierarchy analysis knows about the v table 
    if (cha->knowsAbout(vtbl)) {
      if (preciseClassName != className) {
        sd_print("More precise class name = %s\n", preciseClassName.c_str());
        int64_t ind = cha->getSubVTableIndex(preciseClassName, className);
        SDLayoutBuilder::vtbl_name_t n = preciseClassName;

        if (ind == -1) {
          //className is the derive and the preciseClassName is the base 
          ind = cha->getSubVTableIndex(className, preciseClassName);
          n = className;
        }

        if (ind != -1) {
          vtbl = SDLayoutBuilder::vtbl_t(n, ind);
        }

        sd_print("Index = %d \n", ind);
      } 
    }

    LLVMContext& C = CI->getContext();                    //Paul: get call inst. context 
    llvm::BasicBlock *BB = CI->getParent();               //Paul: get the parent 
    llvm::Function *F = BB->getParent();                  //Paul: get the parent of the previous BB 
    llvm::Type *Int8PtrTy = IntegerType::getInt8PtrTy(C); //Paul: convert the context to  

    //Paul: split the success BB
    llvm::BasicBlock *SuccessBB = BB->splitBasicBlock(CI, "sd.vptr_check.success");
    //Paul: get the old BB terminator 
    llvm::Instruction *oldTerminator = BB->getTerminator();
    IRBuilder<> builder(oldTerminator);

    //do a bit cast and store the result in castVptr
    llvm::Value *castVptr = builder.CreateBitCast(vptr, Int8PtrTy);
 
    //Paul: layout builder has a memory range for that v table 
    if (layoutBuilder->hasMemRange(vtbl)) {
      if(!cha->hasAncestor(vtbl)) {
        sd_print("%s\n", vtbl.first.data());
        assert(false);
      }
      
      //get the root of this v table 
      SDLayoutBuilder::vtbl_name_t root = cha->getAncestor(vtbl);
      assert(layoutBuilder->alignmentMap.count(root));

      //determine the alignment value 
      llvm::Constant* alignment = llvm::ConstantInt::get(IntPtrTy, layoutBuilder->alignmentMap[root]);
      
      int i = 0;

      //notice a v table can have multiple ranges 
      std::vector<SDLayoutBuilder::mem_range_t> ranges(layoutBuilder->getMemRange(vtbl));
      std::sort(ranges.begin(), ranges.end(), range_less_than_key()); //Paul: sort the elements in the range 

      uint64_t sum = 0;
      //Paul: iterate throught the ranges and compute width 
      // in oder to insert the check we need only to know the start address and the width
      for (auto rangeIt : ranges) {
        sum += rangeIt.second; //Paul: compute the width of the range 
      }
      
      //printing some statistics 
      sd_print("For VTable: {%s , %d } Emitting: %d range check(s) with total width sum %d \n", 
      vtbl.first.c_str(), vtbl.second, ranges.size(), sum);
  
      //Paul: iterate throught the ranges for one v table at a time 
      for (auto rangeIt : ranges) {
        llvm::Value *start = rangeIt.first;
        llvm::Value *width = llvm::ConstantInt::get(IntPtrTy, rangeIt.second);
        llvm::Value *Args[] = {castVptr, start, width, alignment};
   
        //Paul: create the fast path success, this Intrinsic::sd_subst_check_range function
        // was previously added during code generation 
        llvm::Value* fastPathSuccess = builder.CreateCall(Intrinsic::getDeclaration(M,
                                                     Intrinsic::sd_subst_check_range),
                                                                                Args);

        char blockName[256];
        //give a name to the failed block and attach an increment value to it, i
        snprintf(blockName, sizeof(blockName), "sd.fastcheck.fail.%d", i);

        //Paul: create the fast path failed  
        llvm::BasicBlock *fastCheckFailed = llvm::BasicBlock::Create(F->getContext(), blockName, F);
        
        //Paul: create the the conditional branch and add fast path success, success BB and fast check failed
        llvm::BranchInst *BI = builder.CreateCondBr(fastPathSuccess, SuccessBB, fastCheckFailed);
        llvm::MDBuilder MDB(BI->getContext());

        //Paul: set the branch weights 
        BI->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(
                                              std::numeric_limits<uint32_t>::max(),
                                              std::numeric_limits<uint32_t>::min()));

        //Paul: set the insertion point 
        builder.SetInsertPoint(fastCheckFailed); //Paul: builder set the insertion point
        i++;
      }
    }

    /*
    llvm::BasicBlock *checkFailed = llvm::BasicBlock::Create(F->getContext(), "sd.check.fail", F);
    llvm::Type* argTs[] = { Int8PtrTy, Int8PtrTy };
    llvm::FunctionType *vptr_safeT = llvm::FunctionType::get(llvm::Type::getInt1Ty(C), argTs, false);
    llvm::Constant *vptr_safeF = M->getOrInsertFunction("_Z9vptr_safePKvPKc", vptr_safeT);
    llvm::Value* slowPathSuccess = builder.CreateCall2(
                vptr_safeF,
                castVptr,
                builder.CreateGlobalStringPtr(className));
                // TODO: Add dynamic class name to _vptr_safe as well

    BranchInst *BI = builder.CreateCondBr(slowPathSuccess, SuccessBB, checkFailed);
    llvm::MDBuilder MDB(BI->getContext());
    BI->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(
      std::numeric_limits<uint32_t>::max(),
      std::numeric_limits<uint32_t>::min()));

    builder.SetInsertPoint(checkFailed);
    */
    // Insert Check Failure
    builder.CreateCall(Intrinsic::getDeclaration(M, Intrinsic::trap)); //Paul: insert the check failure trap 
   
    //Paul: this is the an IRBuilder object delclared before the previous for loop
    builder.CreateUnreachable();

    oldTerminator->eraseFromParent();//Paul: remove old terminator
    CI->replaceAllUsesWith(vptr);//Paul: replace all uses with the new v pointer
    CI->eraseFromParent();
  }
}

//Paul: read the v call index and add replace all uses with this new value 
//it uses: 
// Intrinsic::sd_get_vcall_index -> null 
void SDUpdateIndices::handleRemainingSDGetVcallIndex(Module* M) {
  Function *sd_vcall_indexF = M->getFunction(Intrinsic::getName(Intrinsic::sd_get_vcall_index));

  // if the function doesn't exist, do nothing
  if (!sd_vcall_indexF)
    return;

  // for each use of the function
  for (const Use &U : sd_vcall_indexF->uses()) {
    
    // get the call inst
    llvm::CallInst* CI = cast<CallInst>(U.getUser());

    // get the arguments, Paul: this argument is the v pointer.
    llvm::ConstantInt* arg1 = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    assert(arg1); 

    // since the result of the call instruction is i64, replace all of its occurence with this one
    CI->replaceAllUsesWith(arg1);
  }
}


  /**
   * P5 Module pass for substittuing the final subst_ intrinsics
   */
  class SDSubstModule : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid

    SDSubstModule() : ModulePass(ID) {
      sd_print("initializing SDSubstModule pass\n");
      initializeSDSubstModulePass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDSubstModule() {
      sd_print("deleting SDSubstModule pass\n");
    }

    bool runOnModule(Module &M) {
      sd_print("P5. Started running SDSubstModule pass ...\n");
      sd_print("P5. Starting final range checks additions ...\n");
      
      //Paul: count the number of indexes substituted
      int64_t indexSubst = 0;

      //Paul: count number of range substituted
      int64_t rangeSubst = 0;

      //Paul: count number of equalities substituted
      int64_t eqSubst = 0; 

      //Paul: count the number of constant pointers
      int64_t constPtr = 0;

      //Paul: cum up the width of a range such that
      // we can compute an average value for each inserted check
      uint64_t sumWidth = 0.0;

      //Paul: substitute the v table index
      //get the function used to substitute the v table index 
      Function *sd_subst_indexF = M.getFunction(Intrinsic::getName(Intrinsic::sd_subst_vtbl_index));

       //Paul: substitute the v table range  
       //get the function used to subsitute the range check 
       Function *sd_subst_rangeF = M.getFunction(Intrinsic::getName(Intrinsic::sd_subst_check_range));
      
      // Paul: write the v pointer value back from all the functions which will
      // be called based on this pointer
      if (sd_subst_indexF) {
        for (const Use &U : sd_subst_indexF->uses()) {
          // get the call inst,
          //Returns the User that contains this Use.
          //For an instruction operand, for example, this will return the instruction.
          llvm::CallInst* CI = cast<CallInst>(U.getUser());//Paul: read this value back from code 

          // Paul: get the first arguments, this is the v pointer
          llvm::ConstantInt* arg1 = dyn_cast<ConstantInt>(CI->getArgOperand(0));
          assert(arg1);
          CI->replaceAllUsesWith(arg1);//Paul: write the v pointer back 
          CI->eraseFromParent();

          //count the total number of v pointer index substitutions 
          indexSubst += 1; 
        }
      }
      
      //Paul: add the final range checks 
      //Notice: that we have ranges with: width > 1 or < 1
      if (sd_subst_rangeF) {
        const DataLayout &DL = M.getDataLayout();
        LLVMContext& C = M.getContext();        //module context 
        Type *IntPtrTy = DL.getIntPtrType(C, 0);//return the context as an pointer type 
        
        //coun number of ranges added
        int rangeCounter = 0;

        //count number of constant checks added
        int constantCounter = 0;

        //Paul: for all the places where the range check has to be added
        for (const Use &U : sd_subst_rangeF->uses()) {
          
          // get the call inst
          llvm::CallInst* CI = cast<CallInst>(U.getUser());
          IRBuilder<> builder(CI);

          // get the arguments, this have been writen during the pass P4 from above 
          llvm::Value* vptr            = CI->getArgOperand(0);
          llvm::Constant* start        = dyn_cast<Constant>(CI->getArgOperand(1));
          llvm::ConstantInt* width     = dyn_cast<ConstantInt>(CI->getArgOperand(2));
          llvm::ConstantInt* alignment = dyn_cast<ConstantInt>(CI->getArgOperand(3));

          //all three values > 0
          assert(vptr && start && width);
          
          //get the value as a 64-bit unsigned integer after it has been sign extended
          //as appropriate for the type of this constant 
          int64_t widthInt = width->getSExtValue();
          int64_t alignmentInt = alignment->getSExtValue();
          int alignmentBits = floor(log(alignmentInt + 0.5)/log(2.0));

          llvm::Constant* rootVtblInt = dyn_cast<llvm::Constant>(start->getOperand(0));
          llvm::GlobalVariable* rootVtbl = dyn_cast<llvm::GlobalVariable>(
            rootVtblInt->getOperand(0));
          llvm::ConstantInt* startOff = dyn_cast<llvm::ConstantInt>(start->getOperand(1));
 
          //Paul: sum up all the ranges widths which will be substituted 
          sumWidth = sumWidth + widthInt;

          //check if vptr is constant
          if (validConstVptr(rootVtbl, startOff->getSExtValue(), widthInt, DL, vptr, 0)) {
            
            //replace call instruction with an constant int 
            CI->replaceAllUsesWith(llvm::ConstantInt::getTrue(C));
            CI->eraseFromParent();
           
            //Paul: sum up how many times we had constant pointers 
            constPtr++;
          } else

          //Paul: if the range is grether than 1 do the rotation checks 
          if (widthInt > 1) {
            // create pointer to int
            llvm::Value *vptrInt = builder.CreatePtrToInt(vptr, IntPtrTy);
            
            //substract pointer fram start address
            llvm::Value *diff = builder.CreateSub(vptrInt, start);
            
            //shift right diff with the number of alignmentBits
            llvm::Value *diffShr = builder.CreateLShr(diff, alignmentBits);

            //shift left diff with the number of DL.getPointerSizeInBits(0) - alignmentBits
            llvm::Value *diffShl = builder.CreateShl(diff, DL.getPointerSizeInBits(0) - alignmentBits);

            //create diff rotation 
            llvm::Value *diffRor = builder.CreateOr(diffShr, diffShl);
            
            //create comparison, diffRor <= width 
            llvm::Value *inRange = builder.CreateICmpULE(diffRor, width); //Paul: create a comparison expr.
            
            //replace the in range check 
            CI->replaceAllUsesWith(inRange);

            //CI remove from parent 
            CI->eraseFromParent();
            
            //count the number of range substitutions added
            rangeSubst += 1;

            sd_print("Range: %d has width: % d start: %d vptrInt: %d \n", rangeSubst, widthInt, start, vptrInt);

            //Paul: range = 1 or 0
          } else {
            llvm::Value *vptrInt = builder.CreatePtrToInt(vptr, IntPtrTy);

            //create comparison, v pointer == start  
            llvm::Value *inRange = builder.CreateICmpEQ(vptrInt, start);

            CI->replaceAllUsesWith(inRange);
            CI->eraseFromParent();
            
            eqSubst += 1; //count number of equalities substitutions added
          }        
        }
      }
      
      //in the interleaving paper the average number of ranges per call site was close to 1 (1,005)
      sd_print("P5. Finished running SDSubstModule pass...\n");
      sd_print(" ---SDSubst Statistics--- \n");
      sd_print(" indices %d \n", indexSubst);
      sd_print(" range checks added %d \n", rangeSubst);
      sd_print(" eq_checks added %d \n", eqSubst);
      sd_print(" const_ptr % d \n", constPtr);
      sd_print(" average range width % lf \n", sumWidth * 1.0 / (rangeSubst + eqSubst + constPtr));

      //one of these values has to be > than 0 
      return indexSubst > 0 || rangeSubst > 0 || eqSubst > 0 || constPtr > 0;
    }

//Paul: this validates a constant pointer 
//it is only true if start <= off && off < (start + width * 8) evaluates to true 
 bool validConstVptr(GlobalVariable *rootVtbl, 
                                int64_t start, 
                                int64_t width,
                         const DataLayout &DL, 
                                     Value *V, 
                              uint64_t off) { //initial value is 0 

      if (auto GV = dyn_cast<GlobalVariable>(V)) {
        if (GV != rootVtbl)
          return false;

        if (off % 8 != 0)
          return false;
        
        //Paul: this is the only place that the check can get true in this method 
        return start <= off && off < (start + width * 8);
      }

      if (auto GEP = dyn_cast<GEPOperator>(V)) {
        APInt APOffset(DL.getPointerSizeInBits(0), 0);
        bool Result = GEP->accumulateConstantOffset(DL, APOffset);
        if (!Result)
          return false;
        
        //sum up the offset, 
        //getZExtValue() - get the value as a 64-bit unsigned integer after is was zero extended
        //as appropriate for the type of this constant 
        off += APOffset.getZExtValue();
        return validConstVptr(rootVtbl, start, width, DL, GEP->getPointerOperand(), off); //recursive call 
      }
      
      //check the operand type 
      if (auto Op = dyn_cast<Operator>(V)) {
        if (Op->getOpcode() == Instruction::BitCast)//bitcast operation
          return validConstVptr(rootVtbl, start, width, DL, Op->getOperand(0), off);//recursive call

        if (Op->getOpcode() == Instruction::Select)//select operation
          return validConstVptr(rootVtbl, start, width, DL, Op->getOperand(1), off) &&
                 validConstVptr(rootVtbl, start, width, DL, Op->getOperand(2), off); //two recursive calls 
      }

      return false;
    }
  };

char SDUpdateIndices::ID = 0;
char SDSubstModule::ID = 0;

INITIALIZE_PASS(SDSubstModule, "sdsdmp", "Module pass for substituting the constant-holding intrinsics generated by sdmp.", false, false)
INITIALIZE_PASS_BEGIN(SDUpdateIndices, "cc", "Change Constant", false, false)
INITIALIZE_PASS_DEPENDENCY(SDLayoutBuilder)
INITIALIZE_PASS_DEPENDENCY(SDBuildCHA)
INITIALIZE_PASS_END(SDUpdateIndices, "cc", "Change Constant", false, false)


ModulePass* llvm::createSDUpdateIndicesPass() {
  return new SDUpdateIndices();
}

ModulePass* llvm::createSDSubstModulePass() {
  return new SDSubstModule();
}

