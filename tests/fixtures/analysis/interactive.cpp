#include "provider.hpp"

int global_value = 7;

int calculate(int input) {
  int nearby = input;
  return provide(nearby) + global_value;
}

int unfinished() {
  return prov
}
