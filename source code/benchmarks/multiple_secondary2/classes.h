#ifndef __CLASSES_H__
#define __CLASSES_H__

struct A {
  int a;
  virtual void f ();
};

struct B  {
  int b;
  virtual void g ();
};

struct B1 : public B  {
  int b1;
//  virtual void g ();
};

struct D  {
  int c;
  virtual void h ();
};

struct C : public A,B1 {
  virtual void o ();
};

struct E: public D, public C {
  virtual void f ();
};

#endif
