#include "sink.hpp"
#include <cstdlib>
namespace fc5mp::client {
cap::Writer& sink(){
  // Thread-safe one-time init (C++11). Deliberately leaked: hooks stay live
  // through process exit, and a destroyed Writer (or a stream lock orphaned by
  // a thread ExitProcess killed mid-write) would crash or hang shutdown. Every
  // frame is already flushed, so nothing is lost.
  static cap::Writer* w = new cap::Writer(std::getenv("FC5MP_CAPTURE") ? std::getenv("FC5MP_CAPTURE")
                                                                     : "fc5mp-capture.bin");
  return *w;
}
}
