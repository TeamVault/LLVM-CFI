#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
  D* d = new D();
  C *c = new C();
  B *b = new B();
  A *a = new A();
  F *f = new F();

  a->f();

  b->g();

  c->f();
  c->g();

  d->f();
  d->e();
  ((C*)d)->g();

  f->e();
  f->f();
  ((C*)f)->g();
  
  return 0;
}
