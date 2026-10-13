#include "../../src/hookcore/hookcore.hpp"
#include <cassert>
int main(){
  bool (*c)(void*,void*,void**) = &fc5mp::hook::create;
  bool (*e)(void*) = &fc5mp::hook::enable;
  (void)c; (void)e;
  // host stub returns false (no MinHook); real behavior exercised live in Task 9
  assert(fc5mp::hook::init()==false);
  return 0;
}
