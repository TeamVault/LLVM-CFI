#include "classes.h"
#include <iostream>

A::~A() { std::cout << "deleted A" << std::endl; }
void A::f() { std::cout << "A::f";
  std::cout << std::endl; }
//void A::g() { std::cout << "A::g" << std::endl; }
void A::h() { std::cout << "A::h" << std::endl; }

B::~B() { std::cout << "deleted B" << std::endl; }
