#include "sink.hpp"
#include <cstdlib>
namespace fc5mp::client {
cap::Writer& sink(){
  // function-local static: thread-safe one-time init (C++11)
  static cap::Writer w(std::getenv("FC5MP_CAPTURE") ? std::getenv("FC5MP_CAPTURE")
                                                   : "fc5mp-capture.bin");
  return w;
}
}
