#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
  D* d = new D();

  d->f();
//  d->g();

  C* c = (C*)d;
  B* b1 = (B*)c;

  E* e = (E*)d;
  B* b2 = (B*)e;

  std::cerr << "b1 vtbl= " << *((void**) b1) << "b2 vtbl= " << *((void**)b2) << "\n";

  b1->g();
  b2->g();
  c->g();

  return 0;
}
