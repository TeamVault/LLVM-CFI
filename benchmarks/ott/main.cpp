// taken from https://en.wikipedia.org/wiki/Run-time_type_information and
// https://en.wikipedia.org/wiki/Dynamic_cast

#include <iostream>
#include "classes.h" 
#include <assert.h>
#include <typeinfo>    // for 'typeid'
#include <string>
 
int main()
{
  Base* basePointer = new Derived();
  void *v = (void*)basePointer;

  Base *basePointer1 = (Base*)v;
  basePointer1->hello();

  Derived *derPointer= (Derived*)v;
  derPointer->bye();

  return 0;
}
