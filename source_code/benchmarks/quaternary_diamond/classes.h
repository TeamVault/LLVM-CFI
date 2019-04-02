#ifndef __CLASSES_H__
#define __CLASSES_H__

#include <iostream>

struct A {
  A();
  virtual ~A();
  virtual void f ();
  virtual void g ();
  virtual void h ();
};

struct B: public virtual A {
  B();
  virtual ~B();
  void f ();
  void h ();
};

struct C: public virtual A {
  C();
  virtual ~C();
  void g ();
  void h ();
};

struct E: public virtual A {
  E();
  virtual ~E();
  void h ();
};

struct F: public virtual A {
  F();
  virtual ~F();
  void h ();
};

struct D: public B, public C, public E, public F {
  D();
  virtual ~D();
  void h ();
};

#endif
