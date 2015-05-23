#include <iostream>
#include "C.h"

C::~C()     { std::cout << "deleted C" << std::endl; }
void C::f() { std::cout << "C::f" << std::endl; }
void C::g() { std::cout << "C::g" << std::endl; }
