#include "classes.h"
#include <iostream>

void A::f() { std::cout << "A::f" << std::endl; }
void A::h() { std::cout << "A::h" << std::endl; }

void B::f() { std::cout << "B::f" << std::endl; }
void B::h() { std::cout << "B::h" << std::endl; }

void C::g() { std::cout << "C::g" << std::endl; }

void D::f() { std::cout << "D::f" << std::endl; }
void D::h() { std::cout << "D::h" << std::endl; }
void D::g() { std::cout << "D::g" << std::endl; }
