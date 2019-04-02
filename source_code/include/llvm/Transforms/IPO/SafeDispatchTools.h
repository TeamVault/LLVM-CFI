#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_TOOLS_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_TOOLS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"

#include <string>

#include "SafeDispatchLog.h"

/*Paul:
helper method from the one underneath
*/
static bool sd_isVtableName_ref(const llvm::StringRef& name) {
  if (name.size() <= 4) {
    // name is too short, cannot be a vtable name
    return false;
  } else if (name.startswith("_ZTV") || name.startswith("_ZTC")) {
    llvm::StringRef rest = name.drop_front(4); // drop the _ZT(C|V) part

    return  (! rest.startswith("S")) &&      // but not from std namespace
        (! rest.startswith("N10__cxxabiv")); // or from __cxxabiv
  }

  return false;
}

/*Paul:
this method is used to check if the name is a real v table name.
This method is used when checking that the extracted metadata from 
the Global Variable is a v table.
*/
static bool sd_isVtableName(std::string& className) {
  //just convert the string to a llvm string ref
  llvm::StringRef name(className);

  return sd_isVtableName_ref(name);
}
#endif

