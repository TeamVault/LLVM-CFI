#include <stdint.h>
#include <iostream>
#include <cstdio>

#include "A.h"
#include "B.h"
#include "C.h"
#include "D.h"

void test_A(A* a) {
  std::cout << "A----------------" << std::endl;
  a->f();
}

void test_B(B* b) {
  test_A(b);

  std::cout << "B----------------" << std::endl;
  b->f();
  b->g();
}

void test_C(C* c) {
  test_A(c);

  std::cout << "C----------------" << std::endl;
  c->f();
  c->g();
}

void test_D(D* d) {
  test_B(d);

  std::cout << "D----------------" << std::endl;
  d->f();
  d->g();
  d->h();
}

int main(int argc, char *argv[])
{
  int lucky = 42;
  printf("42 == %d\n", lucky);

  A* a = new A();
  B* b = new B();
  C* c = new C();
  D* d = new D();

  test_A(a);
  test_B(b);
  test_C(c);
  test_D(d);

  delete a;
  delete b;
  delete c;
  delete d;

  return 0;
}
