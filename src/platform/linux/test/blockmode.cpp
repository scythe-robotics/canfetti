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
  constexpr uint16_t TEST_PDO_COBID = 0x445;

  uint8_t srcData[1000];
  uint8_t dstData[1000];
  memset(srcData, 'x', sizeof srcData);
  srcData[999] = 0;

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
    server.od.insert(TEST_IDX, TEST_SUBIDX, Access::RW, OdBuffer{dstData, sizeof dstData});
  });

  LinuxCoDev dev1(125000);
  LinuxCo client(dev1, CLIENT_NODE_ID, "client");
  client.start("vcan0");
  client.doWithLock([&]() {
    client.addSDOClient(SDO_CHANNEL, SERVER_NODE_ID);
  });

  thread t([&]() {
    Error e = client.blockingWrite(SERVER_NODE_ID, TEST_IDX, TEST_SUBIDX, OdBuffer{srcData, sizeof srcData});
    assert(e == Error::Success);
    assert(memcmp(srcData, dstData, sizeof dstData) == 0);
  });

  t.join();
  this_thread::sleep_for(chrono::milliseconds(100));

  server.doWithLock([&]() {
    assert(server.getActiveTransactionCount() == 0);
    assert(server.getTimerCount() == 0);
  });

  client.doWithLock([&]() {
    assert(client.getActiveTransactionCount() == 0);
    assert(client.getTimerCount() == 0);
  });

  return 0;
}
