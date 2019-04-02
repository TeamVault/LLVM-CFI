#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
  A  
   \
   C B
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


struct C : public A {
  virtual void o ();
};

struct D: public C,B {
  virtual void f ();
};

#endif
