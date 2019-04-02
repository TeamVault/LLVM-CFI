#include "classes.h"
#include <iostream>

void test_A(A* a) {
  std::cout << "A----------------" << std::endl;
  a->f();
  a->g();
  a->h();
}

void test_B(B* b) {
  test_A(b);

  std::cout << "B----------------" << std::endl;
  b->f();
  b->g();
  b->h();
}

void test_C(C* c) {
  test_A(c);

  std::cout << "C----------------" << std::endl;
  c->f();
  c->g();
  c->h();
}

void test_D(D* d) {
  test_B(d);
  test_C(d);

  std::cout << "D----------------" << std::endl;
  d->f();
  d->g();
  d->h();
}

void test_X(X* x) {
  std::cout << "X----------------" << std::endl;
  x->x();
}

void test_E(E* e) {
  test_X(e);
  test_D(e);

  std::cout << "E----------------" << std::endl;
  e->f();
  e->g();
  e->h();
  e->x();
}

int main(int argc, char *argv[])
{
  A* a = new A();
  B* b = new B();
  C* c = new C();
  D* d = new D();
  X* x = new X();
  E* e = new E();

  test_A(a);
  test_B(b);
  test_C(c);
  test_D(d);
  test_X(x);
  test_E(e);

  std::cout << "--- Deleting ----" << std::endl;
  delete a;
  std::cout << "-----------------" << std::endl;
  delete b;
  std::cout << "-----------------" << std::endl;
  delete c;
  std::cout << "-----------------" << std::endl;
  delete d;
  std::cout << "-----------------" << std::endl;
  delete x;
  std::cout << "-----------------" << std::endl;
  delete e;

  return 0;
}
