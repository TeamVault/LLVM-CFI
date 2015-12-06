#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
  A  B
   \/|
   C E
   |/
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


struct C : public A,virtual public B {
  int c;
  virtual void g ();
};

struct E : virtual public B {
  int e;
//  virtual void x();
};

struct D: virtual public C, E {
  int d;
  virtual void f ();
};

#endif
