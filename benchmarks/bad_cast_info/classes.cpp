#include "classes.h"
#include <iostream>

A::~A() { std::cout << "deleted A" << std::endl; }
void A::f() { std::cout << "A::f" << std::endl; }

B::~B() { std::cout << "deleted B" << std::endl; }

C::~C() { std::cout << "deleted C" << std::endl; }
void C::f() { std::cout << "C::f" << std::endl; }
