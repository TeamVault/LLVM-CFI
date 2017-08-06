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

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <sstream>

// you have to modify the following 4 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManagerBuilder.cpp

using namespace llvm;

#define WORD_WIDTH 8

#define NEW_VTABLE_NAME(vtbl) ("_SD" + vtbl)

static Constant* sd_isRTTI(Constant* vtblElement) {
  ConstantExpr* bcExpr = NULL;

  // if this a constant bitcast expression, this might be a vthunk
  if ((bcExpr = dyn_cast<ConstantExpr>(vtblElement)) &&
      bcExpr->getOpcode() == Instruction::BitCast) {
    Constant* operand = bcExpr->getOperand(0);

    // this is a vthunk
    if (operand->getName().startswith("_ZTI")) {
      return operand;
    }
  }

  return NULL;
}

static bool sd_isDestructorName(StringRef name) {
  if (name.size() > 4) {
    unsigned s = name.size();
    return name[s-4] == 'D' &&
        ('0' <= name[s-3] && name[s-3] <= '2') &&
        name.endswith("Ev");
  }

  return false;
}

static Constant* sd_getDestructorFunction(Constant* vtblElement) {
  ConstantExpr* bcExpr = NULL;

  // if this a constant bitcast expression, this might be a vthunk
  if ((bcExpr = dyn_cast<ConstantExpr>(vtblElement)) && bcExpr->getOpcode() == BITCAST_OPCODE) {
    Constant* operand = bcExpr->getOperand(0);

    // this is a vthunk
    if (sd_isDestructorName(operand->getName())) {
      return operand;
    }
  }

  return NULL;
}

namespace {

  class SDFix : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid
    typedef std::pair<unsigned, unsigned> vtbl_pair_t;

    SDFix() : ModulePass(ID) {
      initializeSDFixPass(*PassRegistry::getPassRegistry());
    }

    bool runOnModule(Module &M) {
      module = &M;

      sd_print("P1. Started running fix pass...\nn");

      bool isChanged = fixDestructors2();

      sd_print("P1. Finished running fix pass...\n");

      return isChanged;
    }

  private:
    Module* module;

    /**
     * Fixes the destructor function pointers in the vtables where an undefined
     * destructor is used inside the vtable.
     *
     * D2: base object destructor = deletes the non-static members & non-virtual
     *     base classes
     * D1: complete object destructor = D2 + virtual base classes
     * D0: deleting object destructor = D1 + calls the operator delete
     *
     * when a class doesn't have virtual base class -> D1 = D2
     */
    bool fixDestructors(); // Paul: this fix is not used 

    /**
     * Change D1Ev to D2Ev if it doesn't exist
     */
    bool fixDestructors2(); // Paul: this is the used fix

    /**
     * Figure out the start/end of the subvtables by just looking at the vtable elements
     *
     * !!! NOTE !!!
     * This functions currently just return regions that the function pointers don't
     * overlap. It doesn't properly split the vtable into valid sub-vtables. This
     * level of precision is enough for code inside this file.
     */
    std::vector<vtbl_pair_t> findSubVtables(ConstantArray* vtable);

  };

}

char SDFix::ID = 0;

INITIALIZE_PASS(SDFix, "sdfix", "Hack around LLVM issues", false, false)

ModulePass* llvm::createSDFixPass() {
  return new SDFix();
}

namespace {
  struct DestructorInfo {
    DestructorInfo() :
      function(NULL), index(0), isDefined(false), isAlias(false){}

    DestructorInfo(Constant* destructor, unsigned index) {
      this->index = index;
      this->function = destructor;

      Function* f = NULL;
      GlobalAlias* fAlias = NULL;

      if((f = dyn_cast<Function>(destructor))) {
        isDefined = ! f->isDeclaration();
        isAlias = false;

      } else if((fAlias = dyn_cast<GlobalAlias>(destructor))) {
        Function* aliasee;
        ConstantExpr *cexpr;
        
        if ((cexpr = dyn_cast<ConstantExpr>(fAlias->getAliasee()))) {
          assert(cexpr->isCast());
          aliasee = dyn_cast<Function>(cexpr->getOperand(0));
        } else {
          aliasee = dyn_cast<Function>(fAlias->getAliasee());
        }

        assert(aliasee);
        isDefined = ! aliasee->isDeclaration();
        isAlias = true;
      } else {
        destructor->dump();
        llvm_unreachable("Unhandled destructor type?");
      }
    }

    bool needsReplacement() {
      return function && ! isDefined;
    }

    std::string toStr() const {
      if (function == NULL)
        return "NO DESTRUCTOR";

      std::stringstream oss;

      oss << "#" << index << " "
          << function->getName().str()
          << (isDefined ? "" : " (UNDEF)");

      if (isAlias) {
        oss << " (ALIAS TO "
            << cast<GlobalAlias>(function)->getAliasee()->getName().str()
            << ")";
      }

      return oss.str();
    }

    void removeFunctionDecl() {
      assert(function && ! isAlias);

      Function* f = dyn_cast<Function>(function);
      f->eraseFromParent();
    }

    Function* getFunction() {
      if(function == NULL)
        return NULL;

      assert(! isAlias);

      return cast<Function>(function);
    }

    Constant* function;
    unsigned index;
    bool isDefined;
    bool isAlias;
  };
}

/*Paul:
this fix is not used*/
bool SDFix::fixDestructors() {
  bool replaced = false;

  for (GlobalVariable& gv : module->getGlobalList()) {
    if (! sd_isVtableName_ref(gv.getName()))
      continue;
    else if (! gv.hasInitializer())
      continue;

    Constant* init = gv.getInitializer();
    assert(init);
    ConstantArray* vtable = dyn_cast<ConstantArray>(init);
    assert(vtable);

    // get an idea about the virtual function regions
    std::vector<vtbl_pair_t> vtblRegions = findSubVtables(vtable);

    // for each subvtable
    for(unsigned vtblInd = 0; vtblInd < vtblRegions.size(); vtblInd++) {
      // record the destructors used in the vtable
      std::vector<DestructorInfo> destructors(3);

      vtbl_pair_t p = vtblRegions[vtblInd];

      for(unsigned i=p.first; i<p.second; i++) {
        Constant* destructor = sd_getDestructorFunction(vtable->getOperand(i));

        if (destructor == NULL)
          continue;

        // get the type from its name
        unsigned s = destructor->getName().size();
        char type = destructor->getName()[s-3];
        assert('0' <= type && type <= '2');
        unsigned typeInt = type - '0';

        // store it temporarily
        destructors[typeInt] = DestructorInfo(destructor, i);
      }

      // deleting destructor should always be defined
      assert(! destructors[0].needsReplacement());

      DestructorInfo* d1 = &destructors[1];
      DestructorInfo* d2 = &destructors[2];

      // only one of the rest could be undefined
      assert(! d1->needsReplacement() || ! d2->needsReplacement());

      // if complete object destructor is missing...
      if (d1->needsReplacement()) {
        std::string gv2Name = d1->function->getName();
        unsigned l = gv2Name.length();
        gv2Name = gv2Name.replace(l-3, 1, "2");

        Function* f1 = d1->getFunction();
        assert(f1);
        Function* f2 = module->getFunction(gv2Name);
        assert(f2);

        sd_print("Replacing %s with %s inside %s\n",
                 d1->function->getName().data(),
                 gv2Name.c_str(),
                 gv.getName().data());

        f1->replaceAllUsesWith(f2);
        replaced = true;

      // if base object destructor is missing...
      } else if (d2->needsReplacement()) {
        std::string gv1Name = d2->function->getName();
        unsigned l = gv1Name.length();
        gv1Name = gv1Name.replace(l-3, 1, "1");

        Function* f2 = d2->getFunction();
        assert(f2);
        Function* f1 = module->getFunction(gv1Name);
        assert(f1);

        sd_print("Replacing %s with %s inside %s\n",
                 d2->function->getName().data(),
                 gv1Name.c_str(),
                 gv.getName().data());

        f2->replaceAllUsesWith(f1);
        replaced = true;
      }
    }
  }

  return replaced;
}

/*Paul:
this fix will be used inside this pass*/
bool SDFix::fixDestructors2() {
  bool replaced = false;

  for (GlobalVariable& gv : module->getGlobalList()) {
    if (! sd_isVtableName_ref(gv.getName()))
      continue;
    else if (! gv.hasInitializer())
      continue;

    Constant* init = gv.getInitializer();
    assert(init);
    ConstantArray* vtable = dyn_cast<ConstantArray>(init);
    assert(vtable);

    for(unsigned i=0; i<vtable->getNumOperands(); i++) {
      Constant* destructor = sd_getDestructorFunction(vtable->getOperand(i));

      if (destructor == NULL)
        continue;

      // get the type from its name
      unsigned s = destructor->getName().size();
      char type = destructor->getName()[s-3];
      assert('0' <= type && type <= '2');
      unsigned typeInt = type - '0';

      DestructorInfo di(destructor, i);

      if(di.isDefined)
        continue;

      // this only handles the 1 -> 2 conversion
      if (typeInt != 1)
        continue;

      assert(typeInt == 1);

      Function* f1 = di.getFunction();
      assert(f1);
      std::string gv2Name = f1->getName();
      unsigned l = gv2Name.length();
      gv2Name = gv2Name.replace(l-3, 1, "2"); //replace at position l-3 the letter/number with 2

      Function* f2 = module->getFunction(gv2Name);
      assert(f2 && ! f2->isDeclaration());

      sd_print("Replacing %s with %s inside %s \n",
               f1->getName().data(),
               gv2Name.c_str(),
               gv.getName().data());

      f1->replaceAllUsesWith(f2);
      replaced = true;

    }
  }

  return replaced;
}

std::vector<SDFix::vtbl_pair_t> SDFix::findSubVtables(ConstantArray* vtable) {
  assert(vtable);
  std::vector<unsigned> addrPts;
  std::vector<vtbl_pair_t> subvtables;
  unsigned numElements = vtable->getNumOperands();

  for (unsigned i=0; i<numElements; i++) {
    Constant* element = vtable->getOperand(i);
    if (sd_isRTTI(element) != NULL)
      addrPts.push_back(i+1);
  }

  assert(addrPts.size() > 0);

  if (addrPts.size() == 1) {
    subvtables.push_back(vtbl_pair_t(0, numElements));
    return subvtables;
  }
  addrPts.push_back(numElements);

  unsigned prevStart = 0;
  for (unsigned i=0; i<addrPts.size() - 1; i++) {
    subvtables.push_back(vtbl_pair_t(prevStart, addrPts[i+1]));
    prevStart = addrPts[i];
  }

  return subvtables;
}
