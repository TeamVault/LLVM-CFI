#include "classes.h"

#ifdef part1
void Base::hello()
{
  std::cout << "in Base\n";
}

void Derived::hello()
{
  std::cout << "in Derived\n";
}

int myComparisonMethodForGenericSort (Base& ref1, Base& ref2)
{
  // Note: If the cast is not successful, 
  // RTTI enables the process to throw a bad_cast exception
  Derived& d = dynamic_cast<Derived&>(ref1); //RTTI used here

  return d.compare (dynamic_cast<Derived&>(ref2));
}
#endif

#ifdef part2
void A::methodSpecificToA() 
{ 
  std::cout << "Method specific for A was invoked" << std::endl; 
};

void B::methodSpecificToB()
{
  std::cout << "Method specific for B was invoked" << std::endl; 
};

void my_function(A& my_a)
{
	try
	{
    // cast will be successful only for B type objects.
		B& my_b = dynamic_cast<B&>(my_a); 
		my_b.methodSpecificToB();
	}
	catch (const std::bad_cast& e)
	{
		std::cerr << "  Exception " << e.what() << " thrown." << std::endl;
		std::cerr << "  Object is not of type B" << std::endl;
	}
}
#endif
