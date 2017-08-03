#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_STREAM_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_STREAM_H

#define SD_DEBUG

#include "llvm/Support/raw_ostream.h"

namespace sdLog {

typedef llvm::raw_string_ostream stream_t;

  static inline llvm::raw_ostream &stream() {
#ifdef SD_DEBUG
  llvm::errs() << "SD] ";
  return llvm::errs();
#else
  return llvm::nulls();
#endif
}

static inline llvm::raw_ostream &streamWithoutToken() {
#ifdef SD_DEBUG
  return llvm::errs();
#else
  return llvm::nulls();
#endif
}

static void blankLine() {
#ifdef SD_DEBUG
  llvm::errs() << "\n";
#endif
}

static llvm::StringRef newLine = "\nSD] ";

}

#endif

