#include <iostream>

namespace {
  class B {
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

void run_src1_B() {
  B* b = new B();

  b->f();
  b->g();

  delete b;
}
