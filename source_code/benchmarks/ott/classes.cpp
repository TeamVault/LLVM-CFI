#include "classes.h"

void Base::hello()
{
  std::cout << "in Base\n";
}

void Derived::hello()
{
  std::cout << "in Derived\n";
}

void Derived::bye()
{
  std::cout << "bye derived! " << val << "\n";
}
