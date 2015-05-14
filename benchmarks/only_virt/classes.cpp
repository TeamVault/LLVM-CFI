#include "classes.h"
#include <iostream>

#ifndef TEST_CASE
A::~A() { std::cout << "deleted A" << std::endl; }
B::~B() { std::cout << "deleted B" << std::endl; }
C::~C() { std::cout << "deleted C" << std::endl; }
D::~D() { std::cout << "deleted D" << std::endl; }

void A::f() { std::cout << "A::f" << std::endl; }
void A::h() { std::cout << "A::h" << std::endl; }

void B::f() { std::cout << "B::f" << std::endl; }
void B::h() { std::cout << "B::h" << std::endl; }

void C::g() { std::cout << "C::g" << std::endl; }

void D::f() { std::cout << "D::f" << std::endl; }
void D::h() { std::cout << "D::h" << std::endl; }
void D::g() { std::cout << "D::g" << std::endl; }
#else
A::~A() { std::cout << "deleted A" << std::endl; }
B::~B() { std::cout << "deleted B" << std::endl; }
C::~C() { std::cout << "deleted C" << std::endl; }

void A::f() { std::cout << "A::f" << std::endl; }

void B::f() { std::cout << "B::f" << std::endl; }
void B::g() { std::cout << "B::g" << std::endl; }

void C::f() { std::cout << "C::f" << std::endl; }
void C::g() { std::cout << "C::g" << std::endl; }
void C::h() { std::cout << "C::h" << std::endl; }
#endif
