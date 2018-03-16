#ifndef __CLASSES_H__
#define __CLASSES_H__

struct A {
  int a;
  virtual void f ();
  virtual void h ();
};

struct E {
  int wow;
  virtual void blahblah();
};

struct B: public E, public  A {
  void f ();
  void h ();
};

struct C {
  int bobo;
  virtual A* g ();
};

struct D: virtual public C {
  B* g ();
};

#endif
