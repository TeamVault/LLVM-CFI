#include "classes.h"
#include <iostream>

void A::f() { std::cout << "A::f" << std::endl; }
void A::h() { std::cout << "A::h" << std::endl; }

void B::f() { std::cout << "B::f" << std::endl; }
void B::h() { std::cout << "B::h" << std::endl; }

void E::blahblah() { std::cout << "E::blahblah" << std::endl; }

A* C::g() { return NULL; }

B* D::g() { return NULL; }
