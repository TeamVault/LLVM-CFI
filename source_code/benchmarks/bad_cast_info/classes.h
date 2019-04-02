#ifndef __CLASSES_H__
#define __CLASSES_H__

class A {
public:
  virtual ~A();
  virtual void f();
  int i;
};

class B : public A {
public:
  int b;
  virtual ~B();
};

class C : public A {
public:
  int c;
  virtual ~C();
  virtual void f();
};

#endif
