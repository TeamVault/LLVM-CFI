#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_H

#include <iostream>
#include <cstdio>
#include "llvm/Support/raw_ostream.h"
#include <execinfo.h>
#include <stdarg.h>

#define SD_DEBUG

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

static void
sd_print_trace (void)
{
  void *array[50];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace (array, 50);
  strings = backtrace_symbols (array, size);

  std::cerr << "Obtained " << size << "stack frames.\n";

  for (i = 0; i < size; i++)
    std::cerr << strings[i] << std::endl;

  free (strings);
}

#endif

