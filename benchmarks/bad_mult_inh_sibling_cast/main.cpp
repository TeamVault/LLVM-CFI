#include "classes.h"
#include <iostream>


B*  __attribute__((noinline)) foo(D* d) {
  std::cerr << "d vptr: " << (*(void**)d) << "\n";
  C* c = (C*) d;
  std::cerr << "((C*)d) vptr: " << (*(void**)c) << "\n";
  return (B*) ((void*) c);
}

int main(int argc, char *argv[])
{
  D* d = new D();
  

  B* b = foo(d);
  std::cerr << "(B*)d vptr: " << *((void**) ((B*)d))  << "\n";
  std::cerr << "(C*)d vptr: " << *((void**) ((C*)d))  << "\n";
  std::cerr << " faked (B*)d" << *((void**)b) << "\n";
  b->f();
  return 0;
}
