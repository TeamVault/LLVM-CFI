#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
  D* d = new D();
  C *c = new C();
  B *b = new B();
  A *a = new A();
  E* e = new E();
  F* f = new F();

  B *db = (B*)d;
  A *dba = (A*)db;
  A *da = (A*)d;
  A* ba = (A*)b;

  A* ca = (A*)c;
  C *dc = (C*)d;
  A *dca = (A*)dc;

  std::cerr
    << " (A*)d vtbl= " << *((void**) da)
    << " (A*)(B*)d vtbl= " << *((void**)dba)
    << " (A*)b vtbl= " << *((void**)ba)
    << " (A*)(C*)d vtbl= " << *((void**)dca)
    << " (A*)c vtbl= " << *((void**)ca)
    << "\n";

  //f->hum();

  e->hum();
  return 0;
}
