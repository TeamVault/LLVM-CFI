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

        struct CallSiteInfo {
            string className;
            string preciseName;
            const CallInst *call;
        };

        typedef std::map <std::string, std::vector<CallSiteInfo>> callSite_map_t;

        SDReturnRange() : ModulePass(ID), callSiteDebugLocs(), emittedClassHierarchies() {
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

          locateCallSites(M);
          storeCallSites(M);
          storeClassHierarchy(M);

          sd_print("\n P??. Finished running the ??th pass (SDReturnRange) ...\n");
          return true;
        }

        /*Paul:
        this method is used to get analysis results on which this pass depends*/
        void getAnalysisUsage(AnalysisUsage &AU) const override {
          AU.addRequired<SDBuildCHA>(); //Matt: depends on CHA pass
          AU.setPreservesAll(); //Matt: should preserve the information from the CHA pass
        }

    private:
        SDBuildCHA *cha;
        vector <string> callSiteDebugLocs;
        std::map <std::string, std::set<std::string>> emittedClassHierarchies;

        void locateCallSites(Module &M);

        void addCallSite(const CallInst *checked_vptr_call, CallInst &callSite);

        void createSubclassHierarchy(const SDBuildCHA::vtbl_t &root, std::set<string> &output) ;

        void emitSubclassHierarchyIfNeeded(std::string rootClassName);

        void storeCallSites(Module &M);

        void storeClassHierarchy(Module &M);
    };
}

#endif //LLVM_SAFEDISPATCHRETURNRANGE_H
