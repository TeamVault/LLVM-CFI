#ifndef __CLASSES__H_
#define __CLASSES__H_

#include <iostream>
#include <typeinfo> // For std::bad_cast

class Num
{
public:
  int x;
  Num(int y) : x(y) {}
};

template <class T>
class Base
{
    T &inner;
public:
    Base(T &val) : inner(val) {} 
    virtual ~Base() {} 
    virtual T get();
};
 
#endif
