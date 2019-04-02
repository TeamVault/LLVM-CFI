#include "classes.h"
#include <iostream>

A::~A() {std::cout << "deleting A...\n";}
B::~B() {std::cout << "deleting B...\n";}
C::~C() {std::cout << "deleting C...\n";}
D::~D() {std::cout << "deleting D...\n";}
E::~E() {std::cout << "deleting E...\n";}
X::~X() {std::cout << "deleting X...\n";}

void A::f() { std::cout << "A::f" << std::endl; }
void A::g() { std::cout << "A::g" << std::endl; }
void A::h() { std::cout << "A::h" << std::endl; }
void B::f() { std::cout << "B::f" << std::endl; }
void B::h() { std::cout << "B::h" << std::endl; }
void C::g() { std::cout << "C::g" << std::endl; }
void C::h() { std::cout << "C::h" << std::endl; }
void D::h() { std::cout << "D::h" << std::endl; }
void X::x() { std::cout << "X::x" << std::endl; }
void E::f() { std::cout << "E::f" << std::endl; }
void E::h() { std::cout << "E::h" << std::endl; }
