#include <iostream>
#include "A.h"

A::~A()     { std::cout << "deleted A" << std::endl; }
void A::f() { std::cout << "A::f" << std::endl; }
