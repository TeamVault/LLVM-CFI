#include "classes.h"

#include <stdint.h>
#include <iostream>
#include <cstdio>

int main(int argc, char *argv[])
{
  C* c = new C();

  B* b = (B*)((void*)c);
  b->f();

  return 0;
}
