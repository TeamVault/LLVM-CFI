#ifndef __CLASSES__H_
#define __CLASSES__H_

#include <iostream>
#include <typeinfo> // For std::bad_cast

class Base
{
public:
    Base() {} 
    virtual ~Base() {} 
    virtual void hello();
};
 
class Derived : public Base
{
    int val;
public:
    Derived() : val(42) {}
    void hello();
    virtual void bye();
    int compare (Derived& ref) {return 0;}
};

#endif
