#include <iostream>
#include "classes.h"

namespace {
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

  class C : public B {
  public:
    C() { std::cout << "Creating C2 ..." << std::endl; }
    virtual ~C() { std::cout << "Deleting C2 ..." << std::endl; }

    virtual void f() { std::cout << "C2::f" << std::endl; }
    virtual void h() { std::cout << "C2::h" << std::endl; }
    virtual void g() { std::cout << "C2::g" << std::endl; }
  };
}

A* run_src2_B() {
  B* b = new B();

  b->f();
  b->h();

  return b;
}

A* run_src2_C() {
  C* c = new C();

  c->f();
  c->h();
  c->g();

  return c;
}
