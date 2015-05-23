#include <iostream>
#include "D.h"

D::~D()     { std::cout << "deleted D" << std::endl; }
void D::f() { std::cout << "D::f" << std::endl; }
void D::g() { std::cout << "D::g" << std::endl; }
void D::h() { std::cout << "D::h" << std::endl; }

