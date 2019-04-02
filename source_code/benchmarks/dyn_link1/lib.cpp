#include "classes.h"

#include <stdint.h>
#include <iostream>
#include <cstdio>

class B : public A {
public:
  virtual ~B();
  virtual void f();
  virtual void g();
};

B::~B() { std::cout << "deleted B" << std::endl; }
void B::f() { std::cout << "B::f" << std::endl; }
void B::g() { std::cout << "B::g" << std::endl; }

A* func() {
  return new B();
}
