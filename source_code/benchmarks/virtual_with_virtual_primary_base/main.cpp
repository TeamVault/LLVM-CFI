#include "classes.h"

#include <stdint.h>
#include <iostream>
#include <cstdio>

void foo();

int main(int argc, char *argv[])
{
  foo();

  A* a = new A();
  B* b = new B();
  C* c = new C();
  D* d = new D();

  //uint64_t* ZTVA = *((uint64_t**)a);
  //std::cout << ZTVA << std::endl;
  a->f();

  std::cout << "=====================================" << std::endl;

  //uint64_t* ZTVB = *((uint64_t**)b);
  //std::cout << ZTVB << std::endl;
  b->f();
  b->g();

  std::cout << "=====================================" << std::endl;

  c->f();
  c->h();

  std::cout << "=====================================" << std::endl;

  d->f();
  d->h();
  d->g();
  d->t();

  std::cout << "=====================================" << std::endl;

  A* a1 = (A*) b;
  A* a2 = (A*) c;
  A* a3 = (A*) d;

  a1->f();
  a2->f();
  a3->f();

  return 0;
}
