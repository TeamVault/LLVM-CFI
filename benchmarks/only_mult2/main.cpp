#include "classes.h"
#include <iostream>

void test_A(A* a) {a->f(); }
void test_B(B* b) {test_A(b); b->f(); b->g();}
void test_C(C* c) {c->f(); }
void test_D(D* d) {test_C(d); d->f(); d->g();}
void test_E(E* e) {test_B(e); test_D(e); e->f(); e->g(); e->h();}


int main(int argc, char *argv[])
{
  A* a = new A();
  B* b = new B();
  C* c = new C();
  D* d = new D();
  E* e = new E();

  test_A(a);
  test_B(b);
  test_C(c);
  test_D(d);
  test_E(e);

  delete a;
  delete b;
  delete c;
  delete d;
  delete e;

  return 0;
}
