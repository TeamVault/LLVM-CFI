#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
  D* d = new D();
  C *c = new C();
  B *b = new B();
  A *a = new A();

  a->f();
  b->f();
  c->f();
  d->f();
 
  return 0;
}
