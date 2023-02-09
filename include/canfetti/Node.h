#pragma once

#include "CanDevice.h"
#include "ObjDict.h"
#include "Types.h"

namespace canfetti {

class Node {
 public:
  ObjDict od;
  uint8_t nodeId;
  CanDevice &bus;
  System &sys;

  virtual Error setState(State s) = 0;
  State getState() const { return state; }

 protected:
  State state = State::Bootup;

  Node(CanDevice &d, System &sys, uint8_t nodeId)
      : nodeId(nodeId), bus(d), sys(sys) {}
};

}  // namespace canfetti
