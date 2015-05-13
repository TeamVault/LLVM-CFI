#include "classes.h"
#include <iostream>

A::~A() {std::cout << "deleting A...\n";}
B::~B() {std::cout << "deleting B...\n";}
C::~C() {std::cout << "deleting C...\n";}
D::~D() {std::cout << "deleting D...\n";}

void A::f() { std::cout << "A::f" << std::endl; }
void B::g() { std::cout << "B::g" << std::endl; }

void C::f() { std::cout << "C::f" << std::endl; }
void C::g() { std::cout << "C::g" << std::endl; }
void C::h() { std::cout << "C::h" << std::endl; }

void D::f() { std::cout << "D::f" << std::endl; }
void D::g() { std::cout << "D::g" << std::endl; }
void D::h() { std::cout << "D::h" << std::endl; }

