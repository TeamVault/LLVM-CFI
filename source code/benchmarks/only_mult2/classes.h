#ifndef __CLASSES_H__
#define __CLASSES_H__

struct A {
  virtual void f ();
};

struct B: public  A {
  virtual void f ();
  virtual void g ();
};

struct C {
  virtual void f ();
};

struct D: public C {
  virtual void f ();
  virtual void g ();
};

struct E: public B, public D {
  virtual void f ();
  virtual void g ();
  virtual void h ();
};

#endif
