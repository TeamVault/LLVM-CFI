#include <iostream>
#include "classes.h"

namespace src2 {
  class B : public A {
  public:
    B();
    virtual ~B();

    virtual void f();
    virtual void h();
  };

  B::B()  { std::cout << "Creating B2 ..." << std::endl; }
  B::~B() { std::cout << "Deleting B2 ..." << std::endl; }

  void B::f() { std::cout << "B2::f" << std::endl; }
  void B::h() { std::cout << "B2::h" << std::endl; }
}

A* run_src2_B() {
  src2::B* b = new src2::B();

  b->f();
  b->h();

  return b;
}
