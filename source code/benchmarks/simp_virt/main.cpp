#include "classes.h"
#include <iostream>

int main(int argc, char *argv[])
{
  D* d = new D();

  d->f();
  d->h();
  d->g();

  return 0;
}
