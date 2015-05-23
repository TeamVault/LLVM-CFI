#ifndef __D_H__
#define __D_H__

#include "B.h"

class D : public B {
public:
  virtual ~D();
  virtual void f();
  virtual void g();
  virtual void h();
};

#endif
