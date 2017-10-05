#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_STREAM_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_STREAM_H

#define SD_NORMAL

#include "llvm/Support/raw_ostream.h"

namespace sdLog {

typedef llvm::raw_string_ostream stream_t;

static inline llvm::raw_ostream &log() {
#ifdef SD_STREAM_DEBUG
  llvm::errs() << "SD] ";
  return llvm::errs();
#else
  return llvm::nulls();
#endif
}

static inline llvm::raw_ostream &stream() {
#if defined(SD_STREAM_DEBUG) || defined(SD_NORMAL)
  llvm::errs() << "SD] ";
  return llvm::errs();
#else
  return llvm::nulls();
#endif
}

static inline llvm::raw_ostream &warn() {
#if defined(SD_STREAM_DEBUG) || defined(SD_NORMAL)
  llvm::errs() << "SD WARNING] ";
  return llvm::errs();
#else
  return llvm::nulls();
#endif
}

static inline llvm::raw_ostream &errs() {
  llvm::errs() << "SD ERROR] ";
  return llvm::errs();
}

static inline llvm::raw_ostream &logNoToken() {
#ifdef SD_STREAM_DEBUG
  return llvm::errs();
#else
  return llvm::nulls();
#endif
}

static void blankLine() {
#if defined(SD_STREAM_DEBUG) || defined(SD_NORMAL)
  llvm::errs() << "\n";
  return;
#endif
}

static llvm::StringRef newLine = "\nSD] ";

}

#endif

