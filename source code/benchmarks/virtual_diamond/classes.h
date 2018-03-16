#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
     A
    /|
   B C
   |/
   D
*/

struct A {
  int a;
  virtual void f ();
};

struct B : virtual public A {
  int b;
  virtual void g ();
};


struct C : virtual public A {
  int c;
  virtual void g ();
};

struct D: virtual public B, virtual public C {
  int d;
  virtual void f ();
};

#endif
