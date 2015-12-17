#include "classes.h"

#include <stdint.h>
#include <iostream>
#include <cstdio>

void foo();

int main(int argc, char *argv[])
{
  foo();

  B *b = new B();

  void * v = (void*)b;

  C *c = (C*)v;

  c->f();

  return 0;
}
