#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
  B* b = new B();
  
  C* c = (C*) b;

  c->h();

  return 0;
}
