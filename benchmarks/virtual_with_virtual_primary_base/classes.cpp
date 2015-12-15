#include "classes.h"
#include <iostream>

void A::f() { std::cout << "A::f";
  std::cout << std::endl; }
void B::f() { std::cout << "B::f" << std::endl; }
void B::g() { std::cout << "B::g" << std::endl; }

void C::f() { std::cout << "C::f" << std::endl; }
void C::h() { std::cout << "C::h" << std::endl; }

void E::e() { std::cout << "E::e" << std::endl; }

void D::g() { std::cout << "D::g" << std::endl; }
void D::t() { std::cout << "D::t" << std::endl; }
//void D::f() { std::cout << "D::f" << std::endl; }
void D::h() { std::cout << "D::h" << std::endl; }
