#ifndef __CLASSES_H__
#define __CLASSES_H__

class A {
public:
  virtual ~A();
  virtual void f();
  //virtual void g();
  virtual void h();
  //int i;
};

class B : public A {
public:
  virtual ~B();
  virtual void f();
  virtual void g();
};

class C : public A {
public:
  virtual ~C();
  virtual void f();
  virtual void h();
};

class D : public B {
public:
  virtual ~D();
  virtual void g();
  virtual void t();
  virtual void f();
  virtual void h();
};

#endif
