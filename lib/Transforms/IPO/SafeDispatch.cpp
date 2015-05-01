#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/IRBuilder.h"
#include <vector>

// you have to modify the following files for each additional LLVM pass
// 1. IPO.h and IPO.cpp
// 2. LinkAllPasses.h
// 3. InitializePasses.h

using namespace llvm;

#define DEBUG_TYPE "cc"

#define STORE_OPCODE   28
#define GEP_OPCODE     29
#define BITCAST_OPCODE 44
#define CALL_OPCODE    49

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
      unsigned mdId = module->getMDKindID("sd.class.name");

      std::vector<Instruction*> instructions;
      for(BasicBlock::iterator instItr = BB.begin(); instItr != BB.end(); instItr++) {
        instructions.push_back(instItr);
      }

      for(std::vector<Instruction*>::iterator instItr = instructions.begin();
          instItr != instructions.end(); instItr++) {
        Instruction* inst = *instItr;
        unsigned opcode = inst->getOpcode();

        // change 42 into 43
        if (opcode == STORE_OPCODE) {
          StoreInst* storeInst = dyn_cast_or_null<StoreInst>(inst);
          assert(storeInst);

          Value* storeVal = storeInst->getOperand(0);
          ConstantInt* constIntVal = dyn_cast_or_null<ConstantInt>(storeVal);

          if(constIntVal) {
            if (*(constIntVal->getValue().getRawData()) == 42) {
              IntegerType* intType = constIntVal->getType();
              inst->setOperand(0, ConstantInt::get(intType, 43, false));
              errs() << "changed constant val 42 to 43\n";
            }
          }
        }

        // change virtual function call indices to 0
        if (opcode == GEP_OPCODE) {
          GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(inst);
          llvm::MDNode* mdNode = gepInst->getMetadata(mdId);
          if (mdNode) {
            StringRef className = cast<llvm::MDString>(mdNode->getOperand(0))->getString();
            ConstantInt* index = dyn_cast<ConstantInt>(gepInst->getOperand(1));
            assert(index);
            uint64_t indexVal = *(index->getValue().getRawData());
            uint64_t newIndexVal = indexVal * 2;

            gepInst->setOperand(1, ConstantInt::get(IntegerType::getInt64Ty(gepInst->getContext()),
                                                    newIndexVal,
                                                    false));

            errs() << className << ": from: " << indexVal << ", to: " << newIndexVal << "\n";
          }
        }

        // duplicate printf calls
        if (opcode == CALL_OPCODE) {
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
            errs() << "duplicated printf call\n";
          }
        }
      }

      return true;
    }
  };

  /**
   * Module pass for the SafeDispatch Gold Plugin
   */
  struct SDModule : public ModulePass {
    static char ID; // Pass identification, replacement for typeid

    SDModule() : ModulePass(ID) {
      initializeSDModulePass(*PassRegistry::getPassRegistry());
    }

    bool runOnModule(Module &M) {
      std::vector<GlobalVariable*> vtablesToDelete;

      for (Module::global_iterator itr = M.getGlobalList().begin(); itr != M.getGlobalList().end(); itr++ ) {
        GlobalVariable* globalVar = itr;
        StringRef varName = globalVar->getName();

        if (varName.startswith("_ZTV") &&
            ! varName.startswith("_ZTVN10__cxxabiv")) {
          errs() << varName << ":\n";
          errs() << "old type: ";

          ArrayType* arrType = dyn_cast<ArrayType>(globalVar->getType()->getArrayElementType());
          assert(arrType);

          // assuming there are only 2 entries before vfunptrs
          uint64_t newSize = arrType->getArrayNumElements() * 2 - 2;
          PointerType* vtblElemType = PointerType::get(IntegerType::get(M.getContext(), 8), 0);
          ArrayType* newArrType = ArrayType::get(vtblElemType, newSize);
          errs() << "new type: ";

          assert(globalVar->hasInitializer());
          ConstantArray* vtable = dyn_cast<ConstantArray>(globalVar->getInitializer());
          assert(vtable);

          std::vector<Constant*> newVtableElems;
          newVtableElems.push_back(vtable->getOperand(0));
          newVtableElems.push_back(vtable->getOperand(1));
          for (unsigned vtblInd = 2; vtblInd < vtable->getNumOperands(); vtblInd++) {
            newVtableElems.push_back(vtable->getOperand(vtblInd));
            newVtableElems.push_back(ConstantPointerNull::get(vtblElemType));
          }

          Constant* newVtableInit = ConstantArray::get(newArrType, newVtableElems);

          GlobalVariable* newVtable = new GlobalVariable(M, newArrType, true,
                                                         GlobalValue::ExternalLinkage,
                                                         0, "_SD" + varName);
          newVtable->setAlignment(8);
          newVtable->setInitializer(newVtableInit);

          Constant* zero = ConstantInt::get(M.getContext(), APInt(64, 0));
          Constant* two  = ConstantInt::get(M.getContext(), APInt(64, 2));
          std::vector<Constant*> indices;
          indices.push_back(zero);
          indices.push_back(two);

          errs() << "users:\n";
          for (GlobalVariable::user_iterator userItr = globalVar->user_begin();
               userItr != globalVar->user_end(); userItr++) {
            // this should be a getelementptr
            User* user = *userItr;
            ConstantExpr* userCE = dyn_cast<ConstantExpr>(user);
            assert(userCE && userCE->getOpcode() == GEP_OPCODE);

            Constant* newConst = ConstantExpr::getGetElementPtr(newArrType,
                                                                newVtable, indices, true);

            // this should be a bitcast
            User* uuser = *(user->user_begin());
            ConstantExpr* uuserCE = dyn_cast<ConstantExpr>(uuser);
            assert(uuserCE && uuserCE->getOpcode() == BITCAST_OPCODE);

            Constant* newBitcast = ConstantExpr::getBitCast(newConst, uuser->getType());

            // this should be a store instruction
            unsigned uuuserOpNo = uuser->use_begin()->getOperandNo();
            User* uuuser = *(uuser->user_begin());
            Instruction* uuuserInst = dyn_cast<Instruction>(uuuser);
            assert(uuuserInst && uuuserInst->getOpcode() == STORE_OPCODE);

            uuuser->setOperand(uuuserOpNo, newBitcast);
            errs() << "changed " << varName << " to _SD" << varName << "at: \n";
            uuuser->dump();
          }

          vtablesToDelete.push_back(globalVar);
        }
      }

      for(unsigned i=0; i<vtablesToDelete.size(); i++) {
        vtablesToDelete[i]->eraseFromParent();
      }

      return true;
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
