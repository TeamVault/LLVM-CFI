#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
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

#define STORE_OPCODE 28
#define GEP_OPCODE   29
#define CALL_OPCODE  49

namespace {
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

            gepInst->setOperand(1, ConstantInt::get(IntegerType::getInt64Ty(gepInst->getContext()),
                                                    0,
                                                    false));

            errs() << "changed " << className << "'s " << indexVal << " function call to 0\n";
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

  struct SDModule : public ModulePass {
    static char ID; // Pass identification, replacement for typeid

    SDModule() : ModulePass(ID) {
      initializeSDModulePass(*PassRegistry::getPassRegistry());
    }

    bool runOnModule(Module &M) {
      for (Module::global_iterator itr = M.getGlobalList().begin(); itr != M.getGlobalList().end(); itr++ ) {
        GlobalVariable* globalVar = itr;
        StringRef varName = globalVar->getName();

        if (varName.startswith("_ZTV") &&
            ! varName.startswith("_ZTVN10__cxxabiv")) {
          errs() << varName << ":\n";
          errs() << "old type: ";
          globalVar->getType()->dump();

          ArrayType* arrType = dyn_cast<ArrayType>(globalVar->getType()->getArrayElementType());
          assert(arrType);

          // assuming there are only 2 entries before vfunptrs
          uint64_t newSize = arrType->getArrayNumElements() * 2 - 2;
          PointerType* vtblElemType = PointerType::get(IntegerType::get(M.getContext(), 8), 0);
          ArrayType* newArrType = ArrayType::get(vtblElemType, newSize);
          errs() << "new type: ";
          newArrType->dump();

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

          Constant* newVtable = ConstantArray::get(newArrType, newVtableElems);
          errs() << "old vtable:\n";
          globalVar->getOperand(0)->dump();
          errs() << "new vtable:\n";
          newVtable->dump();
        }
      }
      errs() << "##############################################\n";

      return false;
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
