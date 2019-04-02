#include <iostream>
#include "classes.h"

namespace src1 {

  class B : public A {
  public:
    B();
    virtual ~B();

    virtual void f();
    virtual void g();
  };

  B::B()  { std::cout << "Creating B1 ..." << std::endl; }
  B::~B() { std::cout << "Deleting B1 ..." << std::endl; }

  void B::f() { std::cout << "B1::f" << std::endl; }
  void B::g() { std::cout << "B1::g" << std::endl; }
}

A* run_src1_B() {
  src1::B* b = new src1::B();

  b->f();
  b->g();

  return b;
}
