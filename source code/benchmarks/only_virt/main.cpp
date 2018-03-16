#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
#ifndef TEST_CASE
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

  d->f();
  d->h();
  d->g();

  std::cout << "-----------------" << std::endl;

  A* a1 = (A*) b;
  A* a2 = (A*) c;
  A* a3 = (A*) d;

  a1->f();
  a1->h();
  a2->f();
  a2->h();
  a3->f();
  a3->h();

  std::cout << "-----------------" << std::endl;
  delete a;
  std::cout << "-----------------" << std::endl;
  delete a1;
  std::cout << "-----------------" << std::endl;
  delete a2;
  std::cout << "-----------------" << std::endl;
  delete a3;
#else
  A* a = new A();
  B* b = new B();
  C* c = new C();

  a->f();

  std::cout << "-----------------" << std::endl;

  b->f();
  b->g();

  std::cout << "-----------------" << std::endl;

  c->f();
  c->g();
  c->h();

  std::cout << "-----------------" << std::endl;
  delete a;
  std::cout << "-----------------" << std::endl;
  delete b;
  std::cout << "-----------------" << std::endl;
  delete c;
#endif

  return 0;
}
