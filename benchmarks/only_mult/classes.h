#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
 
  A
  |
  B  C
  \ /
   D
 
 */

struct A {
  virtual void f ();
  virtual void h ();
};

struct B: public  A {
  void f ();
  void h ();
};

struct C {
  virtual void g ();
};

struct D: public B, public C {
  void f ();
  void h ();
  void g ();
};

#endif
