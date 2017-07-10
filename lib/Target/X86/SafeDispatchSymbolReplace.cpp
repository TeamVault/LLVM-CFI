#include "SafeDispatchSymbolReplace.h"

using namespace llvm;

char SDSymbolReplace::ID = 0;

INITIALIZE_PASS(SDSymbolReplace, "sdSymbolReplace", "Replace symbols.", false, false)

FunctionPass* llvm::createSDSymbolReplacePass() {
  return new SDSymbolReplace();
}