#include <pthread.h>
#include <atomic>
#include <cstring>
#include <future>
#include <iomanip>
#include <iostream>
#include <string>
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

  constexpr uint16_t TEST_IDX       = 0x2022;
  constexpr uint8_t TEST_SUBIDX     = 0;

  Logger::logger.debug = true;
  Logger::logger.setLogCallback([](const char* m) {
    char name[64] = {0};
    pthread_getname_np(pthread_self(), name, sizeof(name));
    cout << "[" << name << "] " << dec << m << endl;
  });

  LinuxCoDev dev0(125000);
  LinuxCo server(dev0, SERVER_NODE_ID, "server");
  server.start("vcan0");
  unsigned gen;
  server.doWithLock([&]() {
    server.addSDOServer(SDO_CHANNEL, CLIENT_NODE_ID);
    server.od.insert(TEST_IDX, TEST_SUBIDX, Access::RW, _u8(42));
    assert(server.od.generation(TEST_IDX, TEST_SUBIDX, gen) == Error::Success);
  });

  LinuxCoDev dev1(125000);
  LinuxCo client(dev1, CLIENT_NODE_ID, "client");
  client.start("vcan0");
  client.doWithLock([&]() {
    client.addSDOClient(SDO_CHANNEL, SERVER_NODE_ID);
  });

  thread([&]() {
    Error e = client.blockingWrite(SERVER_NODE_ID, TEST_IDX, TEST_SUBIDX, _u8(42));
    assert(e == Error::Success);
  }).join();

  server.doWithLock([&]() {
    unsigned g;
    assert(server.od.generation(TEST_IDX, TEST_SUBIDX, g) == Error::Success);
    assert(gen != g);
  });

  return 0;
}
