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
#include <vector>

#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

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
  struct SDModule : public ModulePass {
    static char ID; // Pass identification, replacement for typeid
    std::vector<GlobalVariable*> vtablesToDelete;

    SDModule() : ModulePass(ID) {
      initializeSDModulePass(*PassRegistry::getPassRegistry());
    }

    bool runOnModule(Module &M) {
      sd_print("Started safedispatch analysis\n");

      for (Module::global_iterator itr = M.getGlobalList().begin();
           itr != M.getGlobalList().end(); itr++ ) {
        GlobalVariable* globalVar = itr;
        StringRef varName = globalVar->getName();

        if (sd_isVtableName_ref(varName)) {
//          sd_print("Changing vtable of %s\n", varName.bytes_begin());
          if(varName.startswith("_ZTV")){
            NamedMDNode* nmd = M.getNamedMetadata(SD_MD_CLASSINFO(varName));
            assert(nmd);
            MDNode* mdClassName = nmd->getOperand(0);
            assert(dyn_cast<MDString>(mdClassName->getOperand(0)));

//            sd_print("NamedMDNode of %s: %s\n",
//                     varName.bytes_begin(),
//                     cast<MDString>(mdClassName->getOperand(0))->getString().bytes_begin());
          }

          expandVtableVariable(M, globalVar);
          isChanged = true;
        }

        // TODO: do we need to handle VTT's ?
      }

      for(unsigned i=0; i<vtablesToDelete.size(); i++) {
        vtablesToDelete[i]->eraseFromParent();
      }

      return isChanged;
    }

  private:
    bool isChanged = false;

    void expandVtableVariable(Module &M, GlobalVariable* globalVar) {
      StringRef varName = globalVar->getName();

      ArrayType* arrType = dyn_cast<ArrayType>(globalVar->getType()->getArrayElementType());
      assert(arrType);

      // assuming there are only 2 entries before vfunptrs
      uint64_t newSize = arrType->getArrayNumElements() * 2;
      PointerType* vtblElemType = PointerType::get(IntegerType::get(M.getContext(), 8), 0);
      ArrayType* newArrType = ArrayType::get(vtblElemType, newSize);

      assert(globalVar->hasInitializer());
      ConstantArray* vtable = dyn_cast<ConstantArray>(globalVar->getInitializer());
      assert(vtable);

//      printVtable(globalVar);

      std::vector<Constant*> newVtableElems;
      for (unsigned vtblInd = 0; vtblInd < vtable->getNumOperands(); vtblInd++) {
        newVtableElems.push_back(vtable->getOperand(vtblInd));
        newVtableElems.push_back(ConstantPointerNull::get(vtblElemType));
      }

      Constant* newVtableInit = ConstantArray::get(newArrType, newVtableElems);

      GlobalVariable* newVtable = new GlobalVariable(M, newArrType, true, globalVar->getLinkage(),
                                                     nullptr, "_SD" + varName, nullptr,
                                                     globalVar->getThreadLocalMode(),
                                                     0,globalVar->isExternallyInitialized());
      newVtable->setAlignment(8);
      newVtable->setInitializer(newVtableInit);

      // create the new vtable base offset
      Constant* zero = ConstantInt::get(M.getContext(), APInt(64, 0));

      for (GlobalVariable::user_iterator userItr = globalVar->user_begin();
           userItr != globalVar->user_end(); userItr++) {
        // this should be a getelementptr
        User* user = *userItr;
        ConstantExpr* userCE = dyn_cast<ConstantExpr>(user);
        assert(userCE && userCE->getOpcode() == GEP_OPCODE);

        ConstantInt* oldConst = dyn_cast<ConstantInt>(userCE->getOperand(2));
        assert(oldConst);

        int64_t oldOffset = oldConst->getSExtValue();
        Constant* newOffset  = ConstantInt::getSigned(Type::getInt64Ty(M.getContext()), oldOffset * 2);

        std::vector<Constant*> indices;
        indices.push_back(zero);
        indices.push_back(newOffset);

        Constant* newConst = ConstantExpr::getGetElementPtr(newArrType, newVtable, indices, true);

        userCE->replaceAllUsesWith(newConst);
      }

      vtablesToDelete.push_back(globalVar);
    }

    void printVtable(GlobalVariable* globalVar) {
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

bool llvm::sd_isVTTName(StringRef& name) {
  return name.startswith("_ZTT");
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
