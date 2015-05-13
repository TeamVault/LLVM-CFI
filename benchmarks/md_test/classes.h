#ifndef __CLASSES_H__
#define __CLASSES_H__
struct A {
  virtual ~A();
  virtual void f ();
  int ia;
};

struct B {
  virtual ~B();
  virtual void g ();
  int ib;
};

struct C: public virtual A, public virtual B {
  virtual ~C();
  virtual void f ();
  virtual void g ();
  virtual void h ();
  int ic;
};

struct D: public C {
  virtual ~D();
  virtual void f ();
  virtual void g ();
  virtual void h ();
  int id;
};

#endif
