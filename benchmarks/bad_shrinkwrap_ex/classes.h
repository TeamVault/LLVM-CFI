#ifndef __CLASSES_H__
#define __CLASSES_H__

/*
 
  A1  A2  A3
   \   \ /
    \   B
     \ /
      c
 
 */

class A1 {
  public:
  int a1;
  virtual void f_A1();
};

class A2 {
  public:
  int a2;
  virtual void f_A2();
};

class A3 {
  public:
  int a3;
  virtual void f_A3() volatile;
};

class B : public A2, public A3{
  public:
  int b;
  virtual void f_B();
};

class C : public A1, public B{
  public:
  int c;
  virtual void f_C();
};

#endif
