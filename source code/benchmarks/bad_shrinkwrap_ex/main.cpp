#include "classes.h"
#include <iostream>


A3 __attribute__((noinline)) *foo() {
  return new A3();
}

int main(int argc, char *argv[])
{
  volatile A1 *a1 = new A1();
  volatile A2 *a2 = new A2();
  volatile A3 *a3 = foo();
  volatile B* b = new B();
  volatile C* c = new C();

//  *((void**)a3) = *((void**)b);
  *((void**)a3) = *((void**)c);
  a3->f_A3();
  return 0;
}
