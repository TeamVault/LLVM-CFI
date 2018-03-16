#include "classes.h"
#include <iostream>


D*  __attribute__((noinline)) foo(D* d) {
  std::cerr << "d vptr: " << (*(void**)d) << "\n";
  C* c = (C*) d;
  std::cerr << "((C*)d) vptr: " << (*(void**)c) << "\n";
  return (D*) ((void*) c);
}

int main(int argc, char *argv[])
{
  D* d = new D();


  D* d1 = foo(d);
  std::cerr << "(B*)d vptr: " << *((void**) ((B*)d))  << "\n";
  std::cerr << "(C*)d vptr: " << *((void**) ((C*)d))  << "\n";
  std::cerr << " faked d" << *((void**)d1) << "\n";
  d1->f();
  return 0;
}
