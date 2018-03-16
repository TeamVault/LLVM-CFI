#include "classes.h"
#include <iostream>

void A::f() { std::cout << "A::f" << std::endl; }

void B::f() { std::cout << "B::f" << std::endl; }
void B::g() { std::cout << "B::g" << std::endl; }

void C::f() { std::cout << "C::f" << std::endl; }

void D::f() { std::cout << "D::f" << std::endl; }
void D::g() { std::cout << "D::g" << std::endl; }

void E::f() { std::cout << "E::f" << std::endl; }
void E::g() { std::cout << "E::g" << std::endl; }
void E::h() { std::cout << "E::h" << std::endl; }
