#ifndef LLVM_SAFEDISPATCHRETURNRANGE_H
#define LLVM_SAFEDISPATCHRETURNRANGE_H

#include "llvm/Transforms/IPO/SafeDispatchCHA.h"
#include "llvm/Transforms/IPO/SafeDispatchLog.h"

// you have to modify the following 5 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManagerBuilder.cpp

using namespace std;

namespace llvm {
    /**
     * This pass collects the valid return addresses for each class and builds the
     * ranges for the returnAddress range check
     * */
    struct SDReturnRange : public ModulePass {
    public:
        static char ID; // Pass identification, replacement for typeid

        SDReturnRange() : ModulePass(ID), callSites() {
          sd_print("initializing SDReturnRange pass\n");
          initializeSDReturnRangePass(*PassRegistry::getPassRegistry());
        }

        virtual ~SDReturnRange() {
          sd_print("deleting SDReturnRange pass\n");
        }

        bool runOnModule(Module &M) override {
          //Matt: get the results from the class hierarchy analysis pass
          cha = &getAnalysis<SDBuildCHA>();

          //TODO MATT: fix pass number
          sd_print("\n P??. Started running the ??th pass (SDReturnRange) ...\n");

          locateCallSites(&M);
          printCallSites();

          sd_print("\n P??. Finished running the ??th pass (SDReturnRange) ...\n");
          return true;
        }

        /*Paul:
        this method is used to get analysis results on which this pass depends*/
        void getAnalysisUsage(AnalysisUsage &AU) const override {
          AU.addRequired<SDBuildCHA>(); //Matt: depends on CHA pass
          AU.setPreservesAll(); //Matt: should preserve the information from the CHA pass
        }

        struct CallSiteInfo {
            string className;
            string preciseName;
            const CallInst* call;
        };

    private:
        SDBuildCHA* cha;
        map<string, vector<CallSiteInfo>> callSites;

        void locateCallSites(Module* M);
        void printCallSites();
        void addCallSite(const CallInst* checked_vptr_call, CallInst* callSite);
        void insertLabel(CallInst* callSite, Module* mod);
    };
}

#endif //LLVM_SAFEDISPATCHRETURNRANGE_H
