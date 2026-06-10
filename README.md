# CANfetti
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

### A CANopen stack by Scythe 🎊

Currently supported platforms:
- Linux
- [Teensy](https://www.pjrc.com/teensy/)
- [ODrive](https://odriverobotics.com/)


## Linux build
```
sudo apt install build-essential cmake libgtest-dev libgmock-dev catkin-tools
mkdir build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./run-tests.sh
```


## Examples

### Teensy Example
```
static constexpr uint8_t MyNodeId = 1;
static float temperatureC = 0.f;
static canfetti::TyCoDev canDevice;
static canfetti::TyCo co(canDevice, MyNodeId, "MyNodeName");

void setup()
{
  tempmon_init();

  co.init();
  co.setHeartbeatPeriod(1000); // Send a NMT heartbeat @ 1Hz

  // TPDO1 @ 10Hz
  co.autoAddTPDO(1, 0x181, 100, canfetti::_p(temperatureC));
}

void loop() {
  co.service(); // Runs the CANopen stack
  ...
  temperatureC = tempmonGetTemp();
  ...
}
```