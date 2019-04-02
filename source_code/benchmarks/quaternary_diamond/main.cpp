#include "classes.h"
#include <iostream>

void call_f(D* d) {
  C* c = (C*) d;
  A* a = (A*) c;
  a->f();
}

int main(int argc, char *argv[]) {
  A* a = new A();
  B* b = new B();
  C* c = new C();
  D* d = new D();
  E* e = new E();
  F* f = new F();

  call_f(d);

  std::cout << "1----------------" << std::endl;
  a->f();
  a->g();
  a->h();
  std::cout << "2----------------" << std::endl;
  b->f();
  b->g();
  b->h();
  std::cout << "3----------------" << std::endl;
  c->f();
  c->g();
  c->h();
  std::cout << "4----------------" << std::endl;
  d->f();
  d->g();
  d->h();

  std::cout << "5----------------" << std::endl;

  A* a1 = (A*) b;
  A* a2 = (A*) c;
  A* a3 = (A*) d;

  a1->f();
  a1->g();
  a1->h();
  a2->f();
  a2->g();
  a2->h();
  a3->f();
  a3->g();
  a3->h();

  std::cout << "6----------------" << std::endl;

  C* c2 = (C*) d;
  c->f();
  c->g();
  c->h();

  std::cout << "7----------------" << std::endl;

  A* a4 = (A*) c2;
  a4->f();
  a4->g();
  a4->h();

  std::cout << "8----------------" << std::endl;
  e->h();
  f->h();

  E *e1 = (E*)d;
  F *f1 = (F*)d;
  e1->h();
  f1->h();

  std::cout << "--- Deleting ----" << std::endl;
  delete a;
  std::cout << "-----------------" << std::endl;
  delete a1;
  std::cout << "-----------------" << std::endl;
  delete a2;
  std::cout << "-----------------" << std::endl;
  delete a4;

  return 0;
}
