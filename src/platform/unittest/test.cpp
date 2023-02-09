#include "canfetti/System.h"

namespace canfetti {
  Logger Logger::logger;

  unsigned newGeneration()
  {
    static unsigned g = 0;
    return g++;
  }
}
