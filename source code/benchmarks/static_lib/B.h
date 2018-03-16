#ifndef __B_H__
#define __B_H__

#include "A.h"

class B : public A {
public:
  virtual ~B();
  virtual void f();
  virtual void g();
};

#endif
