#ifndef __CLASSES__H_
#define __CLASSES__H_

#define part1
#define part2

#include <iostream>
#include <typeinfo> // For std::bad_cast

#ifdef part1
class Base
{
public:
    Base() {} 
    virtual ~Base() {} 
    virtual void hello();
};
 
class Derived : public Base
{
public:
    void hello();
    int compare (Derived& ref) {return 0;}
};

int myComparisonMethodForGenericSort (Base& ref1, Base& ref2);
#endif

#ifdef part2 
class A {
public:
	// Since RTTI is included in the virtual method table there should be at least one virtual function.
	virtual ~A() { };
	void methodSpecificToA();
};
 
class B : public A {
public:
	void methodSpecificToB();
	virtual ~B() {};
};
 
void my_function(A& my_a);
#endif

#endif
