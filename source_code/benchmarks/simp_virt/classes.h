#ifndef __CLASSES_H__
#define __CLASSES_H__


struct A {
  virtual ~A();
  virtual void f ();
  virtual void h ();
};

struct B: public virtual A {
  virtual ~B();
  virtual void f ();
  virtual void h ();
};

struct C: public virtual A {
  virtual ~C();
  virtual void g ();
};

struct D: public virtual B {
  virtual ~D();
  virtual void h ();
  virtual void g ();
};

#endif
