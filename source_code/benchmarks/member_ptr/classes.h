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
    virtual void oneIntArg1(int a);
    virtual void oneIntArg2(int a);
};
 
class Derived : public Base
{
    int val;
public:
    Derived() : val(42) {}
    void hello();
    virtual void bye();
    int compare (Derived& ref) {return 0;}
    virtual void oneIntArg2(int a);
    virtual void oneIntArg3(int a);
};

#endif
