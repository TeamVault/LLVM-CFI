#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_H

#include <stdio.h>
#include "llvm/Support/raw_ostream.h"

static void
sd_print(const char* fmt, ...) {
#ifdef SD_DEBUG
  va_list args;
  va_start(args,fmt);
  fprintf(stderr, "SD] ");
  vfprintf(stderr, fmt,args);
  va_end(args);
#endif
}

#endif

