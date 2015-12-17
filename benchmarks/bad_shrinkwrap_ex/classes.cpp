#include "classes.h"
#include <iostream>

void A1::f_A1() { std::cout << "A1::f_A1" << std::endl; }
void A2::f_A2() { std::cout << "A2::f_A2" << std::endl; }
void A3::f_A3() volatile { std::cout << "A3::f_A3" << std::endl; }
void B::f_B() { std::cout << "B::f_B" << std::endl; }
void C::f_C() { std::cout << "C::f_C" << std::endl; }
