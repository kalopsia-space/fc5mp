#include "../../src/client/versionlock.hpp"
#include <cassert>
int main(){
  using fc5mp::client::build_supported;
  assert(build_supported("00833fae4d5d70213158a146ca98439b28a8e962b934885ecccd906204880af2"));
  assert(build_supported("00833FAE4D5D70213158A146CA98439B28A8E962B934885ECCCD906204880AF2")); // case-insensitive
  assert(!build_supported("deadbeef"));
  assert(!build_supported(""));
  return 0;
}
