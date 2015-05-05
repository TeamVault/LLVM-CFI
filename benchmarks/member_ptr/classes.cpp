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

void Base::oneIntArg1(int a)
{
  std::cout << "Base::oneIntArg1(" << a << ")\n";
}

void Base::oneIntArg2(int a)
{
  std::cout << "Base::oneIntArg2(" << a << ")\n";
}

void Derived::oneIntArg2(int a)
{
  std::cout << "Derived::oneIntArg2(" << a << ")\n";
}

void Derived::oneIntArg3(int a)
{
  std::cout << "Derived::oneIntArg3(" << a << ")\n";
}
