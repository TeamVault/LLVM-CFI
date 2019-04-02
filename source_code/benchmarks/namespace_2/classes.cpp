#include <iostream>
#include "classes.h"

A::A() { std::cout << "Creating A ..." << std::endl; }
A::~A() { std::cout << "Deleting A ..." << std::endl;  }

void A::f() { std::cout << "A::f" << std::endl; }
