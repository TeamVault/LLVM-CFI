#include <iostream>

namespace {
  class B {
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

void run_src2_B() {
  B* b = new B();

  b->f();
  b->h();

  delete b;
}
