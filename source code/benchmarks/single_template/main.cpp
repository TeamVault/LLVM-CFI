// taken from https://en.wikipedia.org/wiki/Run-time_type_information and
// https://en.wikipedia.org/wiki/Dynamic_cast

#include <iostream>
#include "classes.h" 
#include <assert.h>
#include <typeinfo>    // for 'typeid'
#include <string>

template <class T>
T Base<T>::get()
{
  return inner;
}
 
int main()
{
  Num ft = Num(42);
  Base<Num>* bp= new Base<Num>(ft);
  std::cout << bp->get().x << std::endl;
  return 0;
}
