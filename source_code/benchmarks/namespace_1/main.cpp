#include <iostream>
#include "classes.h"

A* run_src1_B();
A* run_src2_B();
A* run_src1_C();
A* run_src2_C();

void test_A(A* a) {
  a->f();
}

int main(int argc, char *argv[])
{
  A* a = new A();
  test_A(a);

  A* a1 = run_src1_B();
  test_A(a1);
  A* a2 = run_src2_B();
  test_A(a2);

  A* a3 = run_src1_C();
  test_A(a3);
  A* a4 = run_src2_C();
  test_A(a4);

  delete a;
  delete a1;
  delete a2;
  delete a3;
  delete a4;

  return 0;
}
