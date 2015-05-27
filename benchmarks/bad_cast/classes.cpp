#include "classes.h"
#include <iostream>

A::A() {std::cout << "constructing A...\n";}
B::B() {std::cout << "constructing B...\n";}
C::C() {std::cout << "constructing C...\n";}

void A::f() { std::cout << "A::f" << std::endl; }
void A::g() { std::cout << "A::g" << std::endl; }
void A::h() { std::cout << "A::h" << std::endl; }
void B::f() { std::cout << "B::f" << std::endl; }
void B::h() { std::cout << "B::h" << std::endl; }
void C::g() { std::cout << "C::g" << std::endl; }
void C::h() { std::cout << "C::h" << std::endl; }
