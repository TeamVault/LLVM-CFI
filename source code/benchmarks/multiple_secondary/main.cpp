#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
  D* d = new D();

  d->o();
  d->f();
  d->g();

  return 0;
}
