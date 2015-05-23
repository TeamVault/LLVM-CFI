#include <iostream>
#include "B.h"

B::~B()     { std::cout << "deleted B" << std::endl; }
void B::f() { std::cout << "B::f" << std::endl; }
void B::g() { std::cout << "B::g" << std::endl; }
