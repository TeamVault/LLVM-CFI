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
  virtual void f ();
};

struct B : virtual public A {
  virtual void g ();
};


struct C : virtual public A {
  //virtual void g ();
};

struct D: virtual public B, virtual public C {
  //virtual void f ();
};

#endif
