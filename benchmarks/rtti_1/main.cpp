// taken from https://en.wikipedia.org/wiki/Run-time_type_information and
// https://en.wikipedia.org/wiki/Dynamic_cast

#include <iostream>
#include "classes.h" 
#include <assert.h>
#include <typeinfo>    // for 'typeid'
#include <string>
 
int main()
{
#ifdef part1
  Base* basePointer = new Derived();

  // directly run typeid
  assert(std::string(typeid(basePointer).name()).find(std::string("Base")) !=
      std::string::npos);
  assert(std::string(typeid(*basePointer).name()).find(std::string("Derived")) !=
      std::string::npos);

  Derived* derivedPointer = NULL;

  // To find whether basePointer is pointing to Derived type of object
  derivedPointer = dynamic_cast<Derived*>(basePointer);

  if (derivedPointer != NULL)
  {
    // Identified
    std::cout << "basePointer is pointing to a Derived class object\n"; 
  }
  else
  {
    std::cout << "basePointer is NOT pointing to a Derived class object\n";
  }

  // Requires virtual destructor 
  delete basePointer;
  basePointer = NULL;
#endif

#ifdef part2  
  A *arrayOfA[3];          // Array of pointers to base class (A)
  arrayOfA[0] = new B();   // Pointer to B object
  arrayOfA[1] = new B();   // Pointer to B object
  arrayOfA[2] = new A();   // Pointer to A object

  for (int i = 0; i < 3; i++)
  {
    // directly call typeid
    assert(std::string(typeid(arrayOfA[i]).name()).find(std::string("A")) !=
        std::string::npos);
    if (i == 2) {
      assert(std::string(typeid(*arrayOfA[i]).name()).find(std::string("A")) !=
          std::string::npos);
    } else {
      assert(std::string(typeid(*arrayOfA[i]).name()).find(std::string("B")) !=
          std::string::npos);
    }

    my_function(*arrayOfA[i]);
    delete arrayOfA[i];  // delete object to prevent memory leak
  }
#endif

  return 0;
}
