#include <sstream>
#include <iostream>

#include "llvm/Pass.h"

#include "llvm/Transforms/IPO/SafeDispatchCHA.h"
#include "llvm/Transforms/IPO/SafeDispatchLog.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"
#include "llvm/Transforms/IPO/SafeDispatchLogStream.h"

// you have to modify the following 4 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManagerBuilder.cpp

using namespace llvm;
using namespace std;

namespace {
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
          AU.addPreserved<SDBuildCHA>(); //Matt: should preserve the information from the CHA pass
        }

        struct CallSiteInfo {
            string className;
            string preciseName;
            const CallInst* call;
        };

    private:
        SDBuildCHA* cha;
        vector<pair<string, CallSiteInfo>> callSites;

        void locateCallSites(Module* M);
        void printCallSites();
        void addCallSite(const CallInst* checked_vptr_call, const CallInst* callSite);
        void insertLabel(CallInst* callSite, Module* mod);
    };
}

char SDReturnRange::ID = 0;

INITIALIZE_PASS(SDReturnRange, "sdRetRange", "Build return ranges.", false, false)

ModulePass* llvm::createSDReturnRangePass() {
  return new SDReturnRange();
}

static string sd_getClassNameFromMD(llvm::MDNode* mdNode, unsigned operandNo = 0) {
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

void SDReturnRange::locateCallSites(Module* M) {
  Function *sd_vtbl_indexF = M->getFunction(Intrinsic::getName(Intrinsic::sd_get_checked_vptr));
  //Function *sd_vtbl_indexF = M->getFunction(Intrinsic::getName(Intrinsic::sd_get_vtbl_index));

  if (!sd_vtbl_indexF){
    //FIXME MATT: intrinsic missing
    sd_print("ERROR");
    return;
  }

  // for each use of the function
  for (const Use &U : sd_vtbl_indexF->uses()) {

    // get each call instruction
    llvm::CallInst *CI = dyn_cast<CallInst>(U.getUser());
    assert(CI);

    //TODO MATT: unsafe use-def chain
    //CI->dump();
    llvm::User *user = *(CI->users().begin());
    for (int i = 0; i < 3; ++i) {
      //user->dump();
      for(User *next : user->users()) {
        user = next;
        break;
      }
    }

    /*
    for(User user : CI->users()) {
      user = user->users();
      break;
    }
    */

    if (CallInst *callSite = dyn_cast<CallInst>(user)) {
      addCallSite(CI, callSite);
      insertLabel(callSite, M);
    }
  }
}

void SDReturnRange::addCallSite(const CallInst* checked_vptr_call, const CallInst* callSite) {
  // get the v ptr
  llvm::Value *vptr = checked_vptr_call->getArgOperand(0);
  assert(vptr);//assert not null

  //Paul: get second operand
  llvm::MetadataAsValue *arg2 = dyn_cast<MetadataAsValue>(checked_vptr_call->getArgOperand(1));
  assert(arg2);//assert not null

  //Paul: get the metadata of the second param
  MDNode *mdNode = dyn_cast<MDNode>(arg2->getMetadata());
  assert(mdNode);//assert not null

  //Paul: get the third parameter
  llvm::MetadataAsValue *arg3 = dyn_cast<MetadataAsValue>(checked_vptr_call->getArgOperand(2));
  assert(arg3);//assert not null

  //Paul: get the metadata of the third param
  MDNode *mdNode1 = dyn_cast<MDNode>(arg3->getMetadata());
  assert(mdNode1);//assert not null

  // second one is the tuple that contains the class name and the corresponding global var.
  // note that the global variable isn't always emitted
  //get the class name class name from argument 1
  string className = sd_getClassNameFromMD(mdNode, 0);

  //get a more precise class name from argument 2
  string preciseName = sd_getClassNameFromMD(mdNode1, 0);

  sdLog::stream() << "ClassName: " << className
                  << ", PreciseName: " << preciseName
                  << ", Callsite:" << *callSite << "\n";

  CallSiteInfo info{className, preciseName, callSite};

  callSites.push_back(pair<string, CallSiteInfo>(preciseName, info));
}

void SDReturnRange::insertLabel(CallInst* callSite, Module* mod) {
  auto parentBB = callSite->getParent();
  parentBB->splitBasicBlock(callSite);
  auto newParent = callSite->getParent();
  newParent->setName(callSite->getName());
  sdLog::stream()  << "Old parent: " << parentBB->getName() << ", new parent: " << newParent->getName() << "\n";

  GlobalVariable* gvar_ptr_abc = new GlobalVariable(/*Module=*/*mod,
          /*Type=*/ Type::getInt8PtrTy(mod->getContext()),
          /*isConstant=*/false,
          /*Linkage=*/GlobalValue::CommonLinkage,
          /*Initializer=*/0, // has initializer, specified below
          /*Name=*/ newParent->getName() );
}


void SDReturnRange::printCallSites() {
  map<string, vector<CallSiteInfo>> groups;
  for (auto element : callSites) {
    groups[element.first].push_back(element.second);
  }

  for (auto element : groups) {
    sdLog::stream() << element.first << ":"  << "\n";
    for (auto callSiteInfo : element.second) {
      sdLog::stream() << *(callSiteInfo.call) << "\n";
    }
  }
}