#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
  A  B
   \/.
 E C .
  \ .' 
   D
   .
   F
*/

struct A {
  int a;
  virtual void f ();
};

struct B {
  int b;
  virtual void g ();
};


struct C : public A, public B {
  int c;
  virtual void g ();
};

struct E {
  int ev;
  virtual void e ();
};

struct D: public E, virtual public C, virtual public B{
  int d;
  virtual void f ();
};

struct F: virtual public D{
  int fv;
};

#endif
