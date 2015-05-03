#ifndef __CLASSES_H__
#define __CLASSES_H__

struct A {
  virtual ~A();
  virtual void f ();
  virtual void g ();
  virtual void h ();
  int ia;
};

struct B: public virtual A {
  virtual ~B();
  void f ();
  void h ();
  int ib;
};

struct C: public virtual A {
  virtual ~C();
  void g ();
  void h ();
  int ic;
};

struct D: public B, public C {
  virtual ~D();
  void h ();
  int id;
};

struct X {
  virtual ~X();
  virtual void x();
  int ix;
};

struct E : X, D {
  virtual ~E();
  void f();
  void h ();
  int ie;
};

#endif
