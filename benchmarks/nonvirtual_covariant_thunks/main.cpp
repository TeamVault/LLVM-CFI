#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
  A* a = new A();
  B* b = new B();
  C* c = new C();
  D* d = new D();

  a->f();
  a->h();
  std::cout << "-----------------" << std::endl;
  b->f();
  b->h();
  std::cout << "-----------------" << std::endl;
  c->g();
  std::cout << "-----------------" << std::endl;
  d->g();

  return 0;
}
