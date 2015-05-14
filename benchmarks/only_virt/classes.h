#ifndef __CLASSES_H__
#define __CLASSES_H__

#define TEST_CASE

#ifndef TEST_CASE

struct A {
  virtual ~A();
  virtual void f ();
  virtual void h ();
};

struct B: public virtual A {
  virtual ~B();
  virtual void f ();
  virtual void h ();
};

struct C: public virtual A {
  virtual ~C();
  virtual void g ();
};

struct D: public virtual B {
  virtual ~D();
  virtual void f ();
  virtual void h ();
  virtual void g ();
};

#else

struct A {
  virtual ~A();
  virtual void f ();
};

struct B: public virtual A {
  virtual ~B();
  virtual void f ();
  virtual void g ();
};

struct C: public virtual B {
  virtual ~C();
  virtual void f ();
  virtual void g ();
  virtual void h ();
};

#endif

#endif
