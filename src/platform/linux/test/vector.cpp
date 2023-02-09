#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>
#include "canfetti/LinuxCo.h"

using namespace std::chrono_literals;
using namespace std;
using namespace canfetti;

int main()
{
  pthread_setname_np(pthread_self(), "main");

  constexpr uint8_t SDO_CHANNEL    = 2;
  constexpr uint8_t SERVER_NODE_ID = 5;
  constexpr uint8_t CLIENT_NODE_ID = 8;

  constexpr uint16_t TEST_IDX   = 0x2022;
  constexpr uint8_t TEST_SUBIDX = 0;

  const std::vector<uint8_t> empty;
  const std::string full_ = "The quick brown fox jumps over the lazy dog";
  const std::vector<uint8_t> full(full_.begin(), full_.end());

  Logger::logger.debug = true;
  Logger::logger.setLogCallback([](const char* m) {
    char name[64] = {0};
    pthread_getname_np(pthread_self(), name, sizeof(name));
    cout << "[" << name << "] " << dec << m << endl;
  });

  LinuxCoDev dev0(125000);
  LinuxCo server(dev0, SERVER_NODE_ID, "server");
  server.start("vcan0");
  server.doWithLock([&]() {
    server.addSDOServer(SDO_CHANNEL, CLIENT_NODE_ID);
    server.od.insert(TEST_IDX, TEST_SUBIDX, Access::RW, empty);
  });

  LinuxCoDev dev1(125000);
  LinuxCo client(dev1, CLIENT_NODE_ID, "client");
  client.start("vcan0");
  client.doWithLock([&]() {
    client.addSDOClient(SDO_CHANNEL, SERVER_NODE_ID);
  });

  {
    server.doWithLock([&]() {
      server.od.set(TEST_IDX, TEST_SUBIDX, empty);
    });
    auto buf = full;
    Error e  = client.blockingRead(SERVER_NODE_ID, TEST_IDX, TEST_SUBIDX, buf);
    assert(e == Error::Success);
    assert(buf == empty);
  }

  {
    server.doWithLock([&]() {
      server.od.set(TEST_IDX, TEST_SUBIDX, full);
    });
    auto buf = empty;
    Error e  = client.blockingRead(SERVER_NODE_ID, TEST_IDX, TEST_SUBIDX, buf);
    assert(e == Error::Success);
    assert(buf == full);
  }

  {
    server.od.set(TEST_IDX, TEST_SUBIDX, full);
    Error e = client.blockingWrite(SERVER_NODE_ID, TEST_IDX, TEST_SUBIDX, vector(empty));
    assert(e == Error::Success);
    auto buf = full;
    server.doWithLock([&]() {
      e = server.od.get(TEST_IDX, TEST_SUBIDX, buf);
    });
    assert(e == Error::Success);
    assert(buf == empty);
  }

  {
    server.od.set(TEST_IDX, TEST_SUBIDX, empty);
    Error e = client.blockingWrite(SERVER_NODE_ID, TEST_IDX, TEST_SUBIDX, vector(full));
    assert(e == Error::Success);
    auto buf = empty;
    server.doWithLock([&]() {
      e = server.od.get(TEST_IDX, TEST_SUBIDX, buf);
    });
    assert(e == Error::Success);
    assert(buf == full);
  }

  return 0;
}
