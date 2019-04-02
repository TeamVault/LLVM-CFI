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

struct C: public  A {
  virtual ~C();
  virtual void f ();
  virtual void g ();
  virtual void h ();
  int ic;
};

struct D: public B {
  virtual ~D();
  virtual void f ();
  virtual void g ();
  virtual void h ();
  int id;
};

struct E: public C, public D {
  virtual ~E();
  virtual void f ();
  virtual void g ();
  virtual void h ();
  virtual void i ();
  int ie;
};

#endif
