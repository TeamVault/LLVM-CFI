#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
  A  B F
   \/|/
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


struct C : public A,B {
  int c;
  virtual void g ();
};

struct F {
  int f;
  virtual void k ();
};

struct E : public B, public F {
  int e;
};

struct D: public C, E {
  int d;
  virtual void f ();
};

#endif
