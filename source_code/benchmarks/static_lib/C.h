#ifndef __C_H__
#define __C_H__

#include "A.h"

class C : public A {
public:
  virtual ~C();
  virtual void f();
  virtual void g();
};

#endif
