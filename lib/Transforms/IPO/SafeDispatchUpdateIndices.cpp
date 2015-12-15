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

// you have to modify the following files for each additional LLVM pass
// 1. IPO.h and IPO.cpp
// 2. LinkAllPasses.h
// 3. InitializePasses.h

using namespace llvm;

namespace {
  /**
   * Pass for updating the annotated instructions with the new indices
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
      layoutBuilder = &getAnalysis<SDLayoutBuilder>();
      cha = &getAnalysis<SDBuildCHA>();
      assert(layoutBuilder);

      sd_print("inside the 2nd pass\n");

      handleSDGetVtblIndex(&M);
      handleSDCheckVtbl(&M);
      handleSDGetCheckedVtbl(&M);
      handleRemainingSDGetVcallIndex(&M);

      sd_print("Finished running the 2nd pass...\n");

      layoutBuilder->removeOldLayouts(M);
      layoutBuilder->clearAnalysisResults();

      sd_print("removed thunks...\n");
      return true;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<SDLayoutBuilder>();
      AU.addRequired<SDBuildCHA>();
      AU.addPreserved<SDBuildCHA>();
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

  /**
   * Module pass for substittuing the final subst_ intrinsics
   */
  class SDSubstModule3 : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid

    SDSubstModule3() : ModulePass(ID) {
      sd_print("initializing SDSubstModule3 pass\n");
      initializeSDSubstModule3Pass(*PassRegistry::getPassRegistry());
    }

    virtual ~SDSubstModule3() {
      sd_print("deleting SDSubstModule3 pass\n");
    }

    bool runOnModule(Module &M) {
      int64_t indexSubst = 0, rangeSubst = 0, eqSubst = 0, constPtr = 0;
      double sumWidth = 0.0;
      Function *sd_subst_indexF =
          M.getFunction(Intrinsic::getName(Intrinsic::sd_subst_vtbl_index));
       Function *sd_subst_rangeF =
          M.getFunction(Intrinsic::getName(Intrinsic::sd_subst_check_range));

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
          llvm::ConstantInt* alignment = dyn_cast<ConstantInt>(CI->getArgOperand(3));
          assert(vptr && start && width);

          int64_t widthInt = width->getSExtValue();
          int64_t alignmentInt = alignment->getSExtValue();
          int alignmentBits = floor(log(alignmentInt + 0.5)/log(2.0));

          llvm::Constant* rootVtblInt = dyn_cast<llvm::Constant>(start->getOperand(0));
          llvm::GlobalVariable* rootVtbl = dyn_cast<llvm::GlobalVariable>(
            rootVtblInt->getOperand(0));
          llvm::ConstantInt* startOff = dyn_cast<llvm::ConstantInt>(start->getOperand(1));

          sumWidth += widthInt;

          if (validConstVptr(rootVtbl, startOff->getSExtValue(), widthInt, DL, vptr, 0)) {
            CI->replaceAllUsesWith(llvm::ConstantInt::getTrue(C));
            CI->eraseFromParent();
            constPtr++;
          } else
          if (widthInt > 1) {
            // Rotate right by 3 to push the lowest order bits into the higher order bits
            llvm::Value *vptrInt = builder.CreatePtrToInt(vptr, IntPtrTy);
            llvm::Value *diff = builder.CreateSub(vptrInt, start);
            llvm::Value *diffShr = builder.CreateLShr(diff, alignmentBits);
            llvm::Value *diffShl = builder.CreateShl(diff, DL.getPointerSizeInBits(0) - alignmentBits);
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

      sd_print("SDSubst: indices: %d ranges: %d eq_checks: %d const_ptr: %d average range: %lf\n", indexSubst, rangeSubst, eqSubst, constPtr, sumWidth/(rangeSubst + eqSubst + constPtr));
      return indexSubst > 0 || rangeSubst > 0 || eqSubst > 0 || constPtr > 0;
    }

    bool validConstVptr(GlobalVariable *rootVtbl, int64_t start, int64_t width,
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
  };

char SDUpdateIndices::ID = 0;
char SDSubstModule3::ID = 0;

INITIALIZE_PASS(SDSubstModule3, "sdsdmp", "Module pass for substituting the constant-holding intrinsics generated by sdmp.", false, false)
INITIALIZE_PASS_BEGIN(SDUpdateIndices, "cc", "Change Constant", false, false)
INITIALIZE_PASS_DEPENDENCY(SDLayoutBuilder)
INITIALIZE_PASS_DEPENDENCY(SDBuildCHA)
INITIALIZE_PASS_END(SDUpdateIndices, "cc", "Change Constant", false, false)


ModulePass* llvm::createSDUpdateIndicesPass() {
  return new SDUpdateIndices();
}

ModulePass* llvm::createSDSubstModule3Pass() {
  return new SDSubstModule3();
}

/// ----------------------------------------------------------------------------
/// SDChangeIndices implementation
/// ----------------------------------------------------------------------------

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

void SDUpdateIndices::handleSDGetVtblIndex(Module* M) {
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

    CI->replaceAllUsesWith(newIntr);
    CI->eraseFromParent();
  }
}

struct range_less_than_key {
  inline bool operator()(const SDLayoutBuilder::mem_range_t &r1, const SDLayoutBuilder::mem_range_t &r2) {
    return r1.second > r2.second; // Invert sign to sort in descending order
  }
};

void SDUpdateIndices::handleSDGetCheckedVtbl(Module* M) {
  Function *sd_vtbl_indexF =
      M->getFunction(Intrinsic::getName(Intrinsic::sd_get_checked_vptr));
  const DataLayout &DL = M->getDataLayout();
  llvm::LLVMContext& C = M->getContext();
  Type *IntPtrTy = DL.getIntPtrType(C, 0);

  // if the function doesn't exist, do nothing
  if (!sd_vtbl_indexF)
    return;

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
    llvm::MetadataAsValue* arg3 = dyn_cast<MetadataAsValue>(CI->getArgOperand(2));
    assert(arg3);
    MDNode* mdNode1 = dyn_cast<MDNode>(arg3->getMetadata());
    assert(mdNode1);

    // second one is the tuple that contains the class name and the corresponding global var.
    // note that the global variable isn't always emitted
    std::string className = sd_getClassNameFromMD(mdNode,0);
    std::string preciseClassName = sd_getClassNameFromMD(mdNode1,0);
    SDLayoutBuilder::vtbl_t vtbl(className, 0);
    llvm::Constant *start;
    int64_t rangeWidth;

    sd_print("Callsite for %s cha->knowsAbout(%s,%d)=%d)\n", className.c_str(),
      vtbl.first.c_str(), vtbl.second, cha->knowsAbout(vtbl));

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

    LLVMContext& C = CI->getContext();
    llvm::BasicBlock *BB = CI->getParent();
    llvm::Function *F = BB->getParent();
    llvm::Type *Int8PtrTy = IntegerType::getInt8PtrTy(C);

    llvm::BasicBlock *SuccessBB = BB->splitBasicBlock(CI, "sd.vptr_check.success");
    llvm::Instruction *oldTerminator = BB->getTerminator();
    IRBuilder<> builder(oldTerminator);
    llvm::Value *castVptr = builder.CreateBitCast(vptr, Int8PtrTy);

    if (layoutBuilder->hasMemRange(vtbl)) {
      if(!cha->hasAncestor(vtbl)) {
        sd_print("%s\n", vtbl.first.data());
        assert(false);
      }

      SDLayoutBuilder::vtbl_name_t root = cha->getAncestor(vtbl);
      assert(layoutBuilder->alignmentMap.count(root));
      llvm::Constant* alignment = llvm::ConstantInt::get(IntPtrTy, layoutBuilder->alignmentMap[root]);
      int i = 0;

      std::vector<SDLayoutBuilder::mem_range_t> ranges(layoutBuilder->getMemRange(vtbl));
      std::sort(ranges.begin(), ranges.end(), range_less_than_key());

      uint64_t sum = 0;
      for (auto rangeIt : ranges) {
        sum += rangeIt.second;
      }

      std::cerr << "{" << vtbl.first.c_str() << "," << vtbl.second
        << "} Emitting " << ranges.size() << " range checks with total width " << sum << "\n";

      for (auto rangeIt : ranges) {
        llvm::Value *start = rangeIt.first;
        llvm::Value *width = llvm::ConstantInt::get(IntPtrTy, rangeIt.second);
        llvm::Value *Args[] = {castVptr, start, width, alignment};
        llvm::Value* fastPathSuccess = builder.CreateCall(Intrinsic::getDeclaration(M,
              Intrinsic::sd_subst_check_range),
              Args);

        char blockName[256];
        snprintf(blockName, sizeof(blockName), "sd.fastcheck.fail.%d", i);
        llvm::BasicBlock *fastCheckFailed = llvm::BasicBlock::Create(F->getContext(), blockName, F);
        builder.CreateCondBr(fastPathSuccess, SuccessBB, fastCheckFailed);
        builder.SetInsertPoint(fastCheckFailed);
        i++;
      }
    }

    llvm::BasicBlock *checkFailed = llvm::BasicBlock::Create(F->getContext(), "sd.check.fail", F);
    llvm::Type* argTs[] = { Int8PtrTy, Int8PtrTy };
    llvm::FunctionType *vptr_safeT = llvm::FunctionType::get(llvm::Type::getInt1Ty(C), argTs, false);
    llvm::Constant *vptr_safeF = M->getOrInsertFunction("_Z9vptr_safePKvPKc", vptr_safeT);
    llvm::Value* slowPathSuccess = builder.CreateCall2(
                vptr_safeF,
                castVptr,
                builder.CreateGlobalStringPtr(className));
                // TODO: Add dynamic class name to _vptr_safe as well
    builder.CreateCondBr(slowPathSuccess, SuccessBB, checkFailed);

    builder.SetInsertPoint(checkFailed);
    // Insert Check Failure
    builder.CreateCall(Intrinsic::getDeclaration(M, Intrinsic::trap));
    builder.CreateUnreachable();

    oldTerminator->eraseFromParent();
    CI->replaceAllUsesWith(vptr);
    CI->eraseFromParent();
  }
}

void SDUpdateIndices::handleSDCheckVtbl(Module* M) {
  Function *sd_vtbl_indexF =
      M->getFunction(Intrinsic::getName(Intrinsic::sd_check_vtbl));
  const DataLayout &DL = M->getDataLayout();
  llvm::LLVMContext& C = M->getContext();
  Type *IntPtrTy = DL.getIntPtrType(C, 0);

  // if the function doesn't exist, do nothing
  if (!sd_vtbl_indexF)
    return;

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
    llvm::MetadataAsValue* arg3 = dyn_cast<MetadataAsValue>(CI->getArgOperand(2));
    assert(arg3);
    MDNode* mdNode1 = dyn_cast<MDNode>(arg3->getMetadata());
    assert(mdNode1);

    // second one is the tuple that contains the class name and the corresponding global var.
    // note that the global variable isn't always emitted
    std::string className = sd_getClassNameFromMD(mdNode,0);
    std::string preciseClassName = sd_getClassNameFromMD(mdNode1,0);
    SDLayoutBuilder::vtbl_t vtbl(className, 0);
    llvm::Constant *start;
    int64_t rangeWidth;

    sd_print("Callsite for %s cha->knowsAbout(%s,%d)=%d) ", className.c_str(),
      vtbl.first.c_str(), vtbl.second, cha->knowsAbout(vtbl));

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

    if (cha->knowsAbout(vtbl) &&
       (!cha->isUndefined(vtbl) || cha->hasFirstDefinedChild(vtbl))) {
      // calculate the new index
      start = cha->isUndefined(vtbl) ?
        layoutBuilder->getVTableRangeStart(cha->getFirstDefinedChild(vtbl)) :
        layoutBuilder->getVTableRangeStart(vtbl);
      rangeWidth = cha->getCloudSize(vtbl.first);
      sd_print(" [rangeWidth=%d start = %p]  \n", rangeWidth, start);
    } else {
      // This is a class we have no metadata about (i.e. doesn't have any
      // non-virtuall subclasses). In a fully statically linked binary we
      // should never be able to create an instance of this.
      start = NULL;
      rangeWidth = 0;
      //std::cerr << "Emitting empty range for " << vtbl.first << "," << vtbl.second << "\n";
      sd_print(" [ no metadata ] \n");
    }
    LLVMContext& C = CI->getContext();

    if (start) {
      IRBuilder<> builder(CI);
      builder.SetInsertPoint(CI);

      //std::cerr << "llvm.sd.callsite.range:" << rangeWidth << std::endl;
        
      // The shift here is implicit since rangeWidth is in terms of indices, not bytes
      llvm::Value *width = llvm::ConstantInt::get(IntPtrTy, rangeWidth);
      llvm::Type *Int8PtrTy = IntegerType::getInt8PtrTy(C);
      llvm::Value *castVptr = builder.CreateBitCast(vptr, Int8PtrTy);

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

      CI->replaceAllUsesWith(newIntr);
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
    } else {
      std::cerr << "llvm.sd.callsite.false:" << vtbl.first << "," << vtbl.second 
        << std::endl;
      CI->replaceAllUsesWith(llvm::ConstantInt::getFalse(C));
      CI->eraseFromParent();
    }
  }
}

void SDUpdateIndices::handleRemainingSDGetVcallIndex(Module* M) {
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
