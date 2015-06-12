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

using namespace llvm;

#define WORD_WIDTH 8

#define NEW_VTABLE_NAME(vtbl) ("_SD" + vtbl)

static bool
sd_isDestructorName(StringRef name) {
  if (name.size() > 4) {
    unsigned s = name.size();
    return name[s-4] == 'D' &&
        ('0' <= name[s-3] && name[s-3] <= '2') &&
        name.endswith("Ev");
  }

  return false;
}

static Constant*
sd_getDestructorFunction(Constant* vtblElement) {
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

    SDFix() : ModulePass(ID) {
      initializeSDFixPass(*PassRegistry::getPassRegistry());
    }

    bool runOnModule(Module &M) {
      module = &M;

      sd_print("Started running fix pass...\n");

      bool isChanged = fixDestructors();

      sd_print("Finished running fix pass...\n");

      return isChanged;
    }

  private:
    Module* module;
    bool fixDestructors();
  };

}

char SDFix::ID = 0;

INITIALIZE_PASS(SDFix, "sdfix", "Hack around LLVM issues", false, false)

ModulePass* llvm::createSDFixPass() {
  return new SDFix();
}

bool SDFix::fixDestructors() {
  Function* f = NULL;
  GlobalAlias* fAlias = NULL;
  for (GlobalVariable& gv : module->getGlobalList()) {
    if (! sd_isVtableName_ref(gv.getName()))
      continue;
    else if (! gv.hasInitializer())
      continue;

    Constant* init = gv.getInitializer();
    assert(init);

    ConstantArray* vtable = dyn_cast<ConstantArray>(init);
    assert(vtable);

    std::set<std::string> destructors;

    for(unsigned i=0; i<vtable->getNumOperands(); i++) {
      Constant* destructor = sd_getDestructorFunction(vtable->getOperand(i));

      if (destructor == NULL)
        continue;
      else if((f = dyn_cast<Function>(destructor))) {
        sd_print("# FUNCTION #####################################\n");
        f->dump();
      } else if((fAlias = dyn_cast<GlobalAlias>(destructor))) {
        const Function* aliasedF = dyn_cast<Function>(fAlias->getAliasee());

        sd_print("# ALIAS ########################################\n");
        fAlias->dump();
        aliasedF->dump();

        assert(!aliasedF->isDeclaration());
      } else {
        destructor->dump();
        llvm_unreachable("Unhandled destructor type?");
      }
    }

  }

  return false;
}

