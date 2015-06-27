#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_CHECK_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_CHECK_H

namespace {
  enum CheckType {
    SD_CHECK_1,       // substract cloud from vtbl, cmp with size, jump
    SD_CHECK_2        // cmp vtbl with both bounds
  };
}

#define SD_CHECK_TYPE SD_CHECK_1

#endif

