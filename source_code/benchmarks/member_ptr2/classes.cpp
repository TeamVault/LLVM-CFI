#include <iostream>

#include "classes.h"

A::A() { std::cout << "creating A" << std::endl; }
B::B() { std::cout << "creating B" << std::endl; }

A::~A() { std::cout << "deleting A" << std::endl; }
B::~B() { std::cout << "deleting B" << std::endl; }

void A::f() { std::cout << "A::f" << std::endl; }
void A::g() { std::cout << "A::g" << std::endl; }
void A::h() { std::cout << "A::h" << std::endl; }
void A::i() { std::cout << "A::i" << std::endl; }

void B::f() { std::cout << "B::f" << std::endl; }
void B::g() { std::cout << "B::g" << std::endl; }
void B::h() { std::cout << "B::h" << std::endl; }
void B::i() { std::cout << "B::i" << std::endl; }
