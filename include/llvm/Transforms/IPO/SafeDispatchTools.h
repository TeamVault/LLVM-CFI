#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_TOOLS_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_TOOLS_H

#include <string>
#include "llvm/ADT/StringRef.h"

static bool sd_isVtableName_ref(llvm::StringRef& name) {
  return (name.startswith("_ZTC") || name.startswith("_ZTV")) && // is a vtable
      (!name.startswith("_ZTVSt")) &&                            // but not from std namespace
      (!name.startswith("_ZTVNSt")) &&                           // but not from std namespace
      (!name.startswith("_ZTVN10__cxxabiv")) &&                  // or from this one
      (!name.startswith("_ZTCSt")) &&                            // but not from std namespace
      (!name.startswith("_ZTCNSt")) &&                           // but not from std namespace
      (!name.startswith("_ZTCN10__cxxabiv"));                    // or from this one
}

static bool sd_isVtableName(std::string& className) {
  llvm::StringRef name(className);

  return sd_isVtableName_ref(name);
}

#endif

