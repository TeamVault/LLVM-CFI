#include "classes.h"

#include <stdint.h>
#include <iostream>
#include <cstdio>

int main(int argc, char *argv[])
{
  A *a = new A();
  A *b = func();

  a->f();
  //printf("%p\n", *(int**)b);
  //*((intptr_t*)b) = (*((intptr_t*)b) + 0x8);
  //printf("%p\n", *(int**)b);
  b->f();
  
  delete b;
  delete a;
  return 0;
}
