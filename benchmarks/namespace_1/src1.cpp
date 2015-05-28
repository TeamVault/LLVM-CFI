#include <iostream>
#include "classes.h"

namespace {

  class B : public A {
  public:
    B()  { std::cout << "Creating B1 ..." << std::endl; }
    virtual ~B() { std::cout << "Deleting B1 ..." << std::endl; }

    virtual void f() { std::cout << "B1::f" << std::endl; }
    virtual void g() { std::cout << "B1::g" << std::endl; }
  };

  class C : public B {
  public:
    C() { std::cout << "Creating C1 ..." << std::endl; }
    virtual ~C() { std::cout << "Deleting C1 ..." << std::endl; }

    virtual void f() { std::cout << "C1::f" << std::endl; }
    virtual void g() { std::cout << "C1::g" << std::endl; }
    virtual void h() { std::cout << "C1::h" << std::endl; }
  };
}

A* run_src1_B() {
  B* b = new B();

  b->f();
  b->g();

  return b;
}

A* run_src1_C() {
  C* c = new C();

  c->f();
  c->g();
  c->h();

  return c;
}
