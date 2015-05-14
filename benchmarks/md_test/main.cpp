#include "classes.h"
#include <iostream>

void test_A(A* a) {
  std::cout << "A----------------" << std::endl;
  a->f();
}

void test_B(B* b) {
  std::cout << "B----------------" << std::endl;
  b->g();
}

void test_C(C* c) {
  test_A(c);
  test_B(c);

  std::cout << "c----------------" << std::endl;
  c->f();
  c->g();
  c->h();
}

void test_D(D* d) {
  test_C(d);

  std::cout << "D----------------" << std::endl;
  d->f();
  d->g();
  d->h();
}

int main(int argc, char *argv[])
{
  A* a = new A();
  B* b = new B();
  C* c = new C();
  D* d = new D();

  test_A(a);
  test_B(b);
  test_C(c);
  test_D(d);

  std::cout << "--- Deleting ----" << std::endl;
  delete a;
  std::cout << "-----------------" << std::endl;
  delete b;
  std::cout << "-----------------" << std::endl;
  delete c;
  std::cout << "-----------------" << std::endl;
  delete d;

  return 0;
}
