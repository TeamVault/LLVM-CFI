#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
  A  B
   \/
   C
   |
   D
*/

struct A {
  int a;
  virtual void f ();
};

struct B  {
  int b;
  virtual void g ();
};


struct C : public A,B {
  virtual void o ();
};

struct D: public C {
  virtual void f ();
};

#endif
