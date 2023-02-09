#include <pthread.h>
#include <atomic>
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

  constexpr uint8_t SDO_1_CHANNEL    = 1;
  constexpr uint8_t SDO_2_CHANNEL    = 2;
  constexpr uint8_t SERVER_NODE_ID   = 5;
  constexpr uint8_t CLIENT_1_NODE_ID = 7;
  constexpr uint8_t CLIENT_2_NODE_ID = 8;

  constexpr uint16_t TEST_IDX1       = 0x2021;
  constexpr uint8_t TEST_SUBIDX1     = 0;
  constexpr uint16_t TEST_PDO_COBID1 = 0x444;
  constexpr uint32_t expectedVal1    = 1234567;

  constexpr uint16_t TEST_IDX2       = 0x2022;
  constexpr uint8_t TEST_SUBIDX2     = 0;
  constexpr uint16_t TEST_PDO_COBID2 = 0x445;
  string expectedVal2                = "a long string yo";

  // logger.logDebug = true;
  Logger::logger.setLogCallback([](const char* m) {
    char name[64] = {0};
    pthread_getname_np(pthread_self(), name, sizeof(name));
    cout << "[" << name << "] " << dec << m << endl;
  });

  LinuxCoDev dev0(125000);
  LinuxCo server(dev0, SERVER_NODE_ID, "server");
  server.start("vcan0");

  LinuxCoDev dev1(125000);
  LinuxCo client1(dev1, CLIENT_1_NODE_ID, "client 1");
  client1.start("vcan0");
  client1.doWithLock([&]() {
    client1.addSDOClient(SDO_1_CHANNEL, SERVER_NODE_ID);
  });
  server.doWithLock([&]() {
    server.addSDOServer(SDO_1_CHANNEL, CLIENT_1_NODE_ID);
    server.od.insert(TEST_IDX1, TEST_SUBIDX1, Access::RW, expectedVal1);
  });

  LinuxCoDev dev2(125000);
  LinuxCo client2(dev2, CLIENT_2_NODE_ID, "client 2");
  client2.start("vcan0");
  client2.doWithLock([&]() {
    client2.addSDOClient(SDO_2_CHANNEL, SERVER_NODE_ID);
  });
  server.doWithLock([&]() {
    server.addSDOServer(SDO_2_CHANNEL, CLIENT_2_NODE_ID);
    server.od.insert(TEST_IDX2, TEST_SUBIDX2, Access::RW, expectedVal2);
  });

  vector<thread> clientThreads;

  for (int i = 0; i < 4; i++) {
    clientThreads.emplace_back([&]() {
      for (int i = 0; i < 500; i++) {
        uint32_t value = 0;
        Error e        = client1.blockingRead(SERVER_NODE_ID, TEST_IDX1, TEST_SUBIDX1, value);
        if (e == Error::Success) {
          assert(value == expectedVal1);
        }

        this_thread::sleep_for(chrono::milliseconds(std::rand() % 10));
      }
    });

    clientThreads.emplace_back([&]() {
      for (int i = 0; i < 500; i++) {
        string value = "aaaaaaaaaaa";
        Error e      = client2.blockingWrite(SERVER_NODE_ID, TEST_IDX2, TEST_SUBIDX2, value);
        if (e == Error::Success) {
          assert(value == "aaaaaaaaaaa");
        }

        this_thread::sleep_for(chrono::milliseconds(std::rand() % 10));
      }
    });
  }

  for (auto& t : clientThreads) {
    t.join();
  }

  this_thread::sleep_for(chrono::milliseconds(100));

  server.doWithLock([&]() {
    assert(server.getActiveTransactionCount() == 0);
    assert(server.getTimerCount() == 0);
  });

  client1.doWithLock([&]() {
    assert(client1.getActiveTransactionCount() == 0);
    assert(client1.getTimerCount() == 0);
  });

  client2.doWithLock([&]() {
    assert(client2.getActiveTransactionCount() == 0);
    assert(client2.getTimerCount() == 0);
  });

  return 0;
}
