#include "A.h"
#include "B.h"
#include "C.h"
#include "D.h"

#include <stdint.h>
#include <iostream>

int main(int argc, char *argv[])
{
  A* a = new A();
  B* b = new B();
  C* c = new C();
  D* d = new D();

  uint64_t* ZTVA = *((uint64_t**)a);
  std::cout << ZTVA << std::endl;
  a->f();
  a->h();

  std::cout << "=====================================" << std::endl;

  uint64_t* ZTVB = *((uint64_t**)b);
  std::cout << ZTVB << std::endl;
  b->f();
  b->h();
  b->g();

  std::cout << "=====================================" << std::endl;

  c->f();
  c->h();

  std::cout << "=====================================" << std::endl;

  d->f();
  d->h();
  d->g();
  d->t();

  return 0;
}
