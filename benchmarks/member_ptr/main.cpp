#include <iostream>
#include "classes.h"
#include <assert.h>
#include <string>

int main()
{
  Base* baseDerivedPointer = new Derived();
  Base* baseBasePointer = new Base();
  Derived* derivedPointer = new Derived();

  void (Base::*bFptr)(int);

  bFptr = &Base::oneIntArg1;

  // These should all be Base::oneIntArg1
  (baseBasePointer->*bFptr)(42);
  (baseDerivedPointer->*bFptr)(43);
  (derivedPointer->*bFptr)(43);

  bFptr = &Base::oneIntArg2;
  // This should be Base::oneIntArg2
  (baseBasePointer->*bFptr)(44);
  // This should be Derived::oneIntArg2
  (baseDerivedPointer->*bFptr)(45);
  // This should be Derived::oneIntArg2
  (derivedPointer->*bFptr)(46);

  void (Derived::*dFptr)(int);
  // This is a type error
  // bFptr = &Derived::oneIntArg3;
  // These two are type errors:
  //(baseBasePointer->*dFptr)(44);
  //(baseDerivedPointer->*dFptr)(45);
  //
  // This isn't, and it works the same
  // way as dFptr = &Derived::oneIntArg1
  dFptr = &Base::oneIntArg2;
  // This should be Derived::oneIntArg2
  (derivedPointer->*dFptr)(47);
  dFptr = &Derived::oneIntArg1;
  (derivedPointer->*dFptr)(48);
  dFptr = &Derived::oneIntArg2;
  (derivedPointer->*dFptr)(49);
  dFptr = &Derived::oneIntArg3;
  (derivedPointer->*dFptr)(50);


  return 0;
}
