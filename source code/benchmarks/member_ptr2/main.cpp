#include <iostream>
#include <string.h>
#include <stdint.h>

#include "classes.h"

typedef void (A::*MemberFunctionPtr)();

struct MP {
  int64_t i1;
  int64_t i2;
};

struct FunPtrSt
{
  MemberFunctionPtr fp1;
  MP mp1;
  MemberFunctionPtr fp2;
};

int main(int argc, char *argv[])
{
  A* a = new A();
  B* b = new B();

  bool cond1 = true;
  bool cond2 = false;

  void (A::*aFptr)();

  FunPtrSt fps1 = {&A::f, {1,2}, &A::g};
  fps1 = (FunPtrSt) {&A::f, {1,2}, &A::g};
  FunPtrSt fps2 = {&A::h, {3,4}, &A::i};

  FunPtrSt fps = cond1 ? (fps1) : (fps2);

  (a->*(fps.fp1))();
  (b->*(fps.fp2))();

  delete a;
  delete b;

  return 0;
}
