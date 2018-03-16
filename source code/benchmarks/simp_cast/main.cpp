#include "classes.h"
#include <stdint.h>
#include <iostream>
#include <cstdio>

int main(int argc, char *argv[])
{
  B* b = new B();
  b->f();

  delete b;
  return 0;
}
