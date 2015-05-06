#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_TRACE_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_TRACE_H

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

#endif // SAFEDISPATCHTRACE_H

