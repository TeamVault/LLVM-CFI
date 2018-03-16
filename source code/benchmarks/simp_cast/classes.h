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
};

#endif
