class A
{
public:
  A ();
  virtual ~A ();

  virtual void f();
  virtual void g();
  virtual void h();
  virtual void i();
};

class B : public A
{
public:
  B ();
  virtual ~B();

  virtual void f();
  virtual void g();
  virtual void h();
  virtual void i();
};
