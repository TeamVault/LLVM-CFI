#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
  A* a = new A();
  B* b = new B();
  C* c = new C();
  D* d = new D();

  a->f();
  a->g();
  a->h();
  std::cout << "-----------------" << std::endl;
  b->f();
  b->g();
  b->h();
  std::cout << "-----------------" << std::endl;
  c->f();
  c->g();
  c->h();
  std::cout << "-----------------" << std::endl;
  d->f();
  d->g();
  d->h();

  return 0;
}
