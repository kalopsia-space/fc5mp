#pragma once
#include "capture/record.hpp"
namespace fc5mp::client {
// The one capture writer for the whole process, opened on first use at
// $FC5MP_CAPTURE (default fc5mp-capture.bin). Address markers and traffic
// hooks share it, so frames from every thread land in one well-ordered file.
cap::Writer& sink();
}
