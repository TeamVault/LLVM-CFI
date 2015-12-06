#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
     X
     .
     A
    /|
   B C
   |/
   D
*/

struct X {
  virtual void f ();
};

struct A {
  int a;
  virtual void f ();
};

struct B : public A {
  int b;
  virtual void g ();
};


struct C : public A {
  int c;
  virtual void g ();
};

struct D: public B, public C {
  int d;
  virtual void f ();
};

#endif
