#ifndef __CLASSES_H__
#define __CLASSES_H__


/*
    A
   . .
  B   C
   \ /
 X  D
  \/
  E
*/

class A {
public:
  virtual ~A();
  virtual void f ();
  virtual void g ();
  virtual void h ();
  int ia;
};

class B: public virtual A {
public:
  virtual ~B();
  void f ();
  void h ();
  int ib;
};

class C: public virtual A {
public:
  virtual ~C();
  void g ();
  void h ();
  int ic;
};

class D: public B, public C {
public:
  virtual ~D();
  void h ();
  int id;
};

class X {
public:
  virtual ~X();
  virtual void x();
  int ix;
};

class E : public X, public D {
public:
  virtual ~E();
  void f();
  void h ();
  int ie;
};

#endif
