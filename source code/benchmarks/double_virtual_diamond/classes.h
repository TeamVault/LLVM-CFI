#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
     A
    /|\
   B C E
   |/  |
   D  /
   \ /
    F
*/

struct A {
  int a;
  virtual void f ();
  virtual void hum();
};

struct B : virtual public A {
  int b;
  virtual void g ();
};


struct C : virtual public A {
  int c;
  virtual void g ();
};

struct E : virtual public A {
  int e;
  virtual void f ();
  virtual void hum();
};

struct D: virtual public B, virtual public C {
  int d;
  virtual void f ();
};

struct F : virtual public E, virtual public D {
  int e;
  virtual void f ();
};

#endif
