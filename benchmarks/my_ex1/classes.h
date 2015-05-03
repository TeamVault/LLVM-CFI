#ifndef __CLASSES_H__
#define __CLASSES_H__

#include <iostream>

struct A {
  A();
  virtual void f ();
  virtual void g ();
  virtual void h ();
};

struct B: public virtual A {
  B();
  void f ();
  void h ();
};

struct C: public virtual A {
  C();
  void g ();
  void h ();
};

struct D: public B, public C {
  D();
  void h ();
};

#endif
