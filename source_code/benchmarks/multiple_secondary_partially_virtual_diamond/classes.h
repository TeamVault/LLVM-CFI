#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
  A  B
   \/ \
   C F \
   |  \|
   |   E
   |  /
    \/
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

struct F {
  int f;
  virtual void y();
};

struct E : public F, B {
  int e;
//  virtual void x();
  virtual void g ();
};

struct D: public C, E {
  int d;
  virtual void f ();
};

#endif
