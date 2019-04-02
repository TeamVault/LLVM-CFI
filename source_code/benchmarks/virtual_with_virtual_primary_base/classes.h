#ifndef __CLASSES_H__
#define __CLASSES_H__

/*

   A
   ..
   B .
   |  .
 E C .
  \..
   D

*/

class A {
public:
  virtual void f();
};

class B : virtual public A {
public:
  virtual void f();
  virtual void g();
};

class C : public B {
public:
  virtual void f();
  virtual void h();
};

class E {
public:
  virtual void e();
};

class D : public E, virtual public C, virtual public A {
public:
  virtual void g();
  virtual void t();
  //virtual void f();
  virtual void h();
};

#endif
