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

struct TestCase {
    const char* name;
    size_t size;
    bool writeTest;
    bool expectSuccess;
};

static void generatePattern(uint8_t* buf, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        buf[i] = static_cast<uint8_t>(i % 256);
    }
}

static bool verifyPattern(const uint8_t* buf, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != static_cast<uint8_t>(i % 256)) {
            return false;
        }
    }
    return true;
}

int main()
{
    pthread_setname_np(pthread_self(), "main");

    constexpr uint8_t SDO_CHANNEL    = 2;
    constexpr uint8_t SERVER_NODE_ID = 5;
    constexpr uint8_t CLIENT_NODE_ID = 8;

    constexpr uint16_t TEST_IDX   = 0x2022;
    constexpr uint8_t  TEST_SUBIDX = 0;

    Logger::logger.debug = true;
    Logger::logger.setLogCallback([](const char* m) {
        char name[64] = {0};
        pthread_getname_np(pthread_self(), name, sizeof(name));
        cout << "[" << name << "] " << dec << m << endl;
    });

    vector<TestCase> testCases = {
        // Small transfers (1 sub-block, few segments)
        {"write_100_bytes",   100, true,  true},
        {"read_100_bytes",    100, false, true},
        {"write_14_bytes",    14,  true,  true},
        {"read_14_bytes",     14,  false, true},
        {"write_15_bytes",    15,  true,  true},
        {"read_15_bytes",     15,  false, true},
        // 7-byte boundary alignment
        {"write_21_bytes",    21,  true,  true},
        {"read_21_bytes",     21,  false, true},
        {"write_896_bytes",   896, true,  true},
        {"read_896_bytes",    896, false, true},
        // Multi sub-block boundaries
        {"write_900_bytes",   900, true,  true},
        {"read_900_bytes",    900, false, true},
        {"write_903_bytes",   903, true,  true},
        {"read_903_bytes",    903, false, true},
        {"write_1792_bytes",  1792, true, true},
        {"read_1792_bytes",   1792, false, true},
    };

    LinuxCoDev dev0(125000);
    LinuxCo server(dev0, SERVER_NODE_ID, "server");
    server.start("vcan0");

    LinuxCoDev dev1(125000);
    LinuxCo client(dev1, CLIENT_NODE_ID, "client");
    client.start("vcan0");

    client.doWithLock([&]() {
        client.addSDOClient(SDO_CHANNEL, SERVER_NODE_ID);
    });

    int passed = 0;
    int failed = 0;
    uint16_t nextIdx = TEST_IDX;

    for (auto& tc : testCases) {
        uint16_t idx = nextIdx++;
        atomic<bool> cbFired{false};
        vector<uint8_t> srcData(tc.size);
        vector<uint8_t> dstData(tc.size, 0);
        generatePattern(srcData.data(), tc.size);
        if (!tc.writeTest) {
            memcpy(dstData.data(), srcData.data(), tc.size);
        }

        server.doWithLock([&]() {
            server.addSDOServer(SDO_CHANNEL, CLIENT_NODE_ID);
            server.od.insert(idx, TEST_SUBIDX, Access::RW,
                             OdBuffer{dstData.data(), dstData.size()},
                             [&](uint16_t, uint8_t) {
                                 cbFired = true;
                             });
        });

        OdBuffer srcBuf{srcData.data(), srcData.size()};
        OdBuffer dstBuf{dstData.data(), dstData.size()};
        Error e = Error::Success;
        thread t([&]() {
            if (tc.writeTest) {
                e = client.blockingWrite(SERVER_NODE_ID, idx, TEST_SUBIDX, srcBuf);
            }
            else {
                e = client.blockingRead(SERVER_NODE_ID, idx, TEST_SUBIDX, dstBuf);
            }
        });
        t.join();

        bool dataOk = false;
        if (tc.writeTest) {
            dataOk = (memcmp(srcData.data(), dstData.data(), tc.size) == 0);
        }
        else {
            dataOk = verifyPattern(dstData.data(), tc.size);
        }

        bool cbOk = tc.expectSuccess && tc.writeTest ? cbFired.load() : !cbFired.load();
        bool errOk = tc.expectSuccess ? (e == Error::Success) : (e != Error::Success);

        bool ok = dataOk && cbOk && errOk;

        if (ok) {
            cout << "[PASS] " << tc.name << endl;
            passed++;
        }
        else {
            cout << "[FAIL] " << tc.name
                 << " err=" << (unsigned)e
                 << " dataOk=" << (dataOk ? "Y" : "N")
                 << " cbFired=" << cbFired.load()
                 << " cbOk=" << (cbOk ? "Y" : "N")
                 << " errOk=" << (errOk ? "Y" : "N")
                 << endl;
            failed++;
        }

        this_thread::sleep_for(10ms);
    }

    // Failure scenarios: callback must NOT fire
    cout << endl << "=== Failure scenarios ===" << endl;

    {
        // Write to RO entry
        atomic<bool> cbFired{false};
        vector<uint8_t> srcData(100);
        vector<uint8_t> dstData(100, 0);
        generatePattern(srcData.data(), srcData.size());
        memcpy(dstData.data(), srcData.data(), srcData.size());
        uint16_t idx = nextIdx++;

        server.doWithLock([&]() {
            server.addSDOServer(SDO_CHANNEL, CLIENT_NODE_ID);
            server.od.insert(idx, TEST_SUBIDX, Access::RO,
                             OdBuffer{dstData.data(), dstData.size()},
                             [&](uint16_t, uint8_t) {
                                 cbFired = true;
                             });
        });

        OdBuffer srcBuf{srcData.data(), srcData.size()};
        Error e = client.blockingWrite(SERVER_NODE_ID, idx, TEST_SUBIDX, srcBuf);

        bool ok = (e != Error::Success) && !cbFired.load();
        if (ok) {
            cout << "[PASS] write_to_ro_no_callback" << endl;
            passed++;
        }
        else {
            cout << "[FAIL] write_to_ro_no_callback"
                 << " err=" << (unsigned)e
                 << " cbFired=" << cbFired.load()
                 << endl;
            failed++;
        }

        this_thread::sleep_for(10ms);
    }

    {
        // Read non-existent index
        atomic<bool> cbFired{false};
        vector<uint8_t> dstData(100, 0);

        OdBuffer dstBuf{dstData.data(), dstData.size()};
        Error e = client.blockingRead(SERVER_NODE_ID, 0x9999, 0, dstBuf);

        bool ok = (e != Error::Success) && !cbFired.load();
        if (ok) {
            cout << "[PASS] read_nonexistent_no_callback" << endl;
            passed++;
        }
        else {
            cout << "[FAIL] read_nonexistent_no_callback"
                 << " err=" << (unsigned)e
                 << " cbFired=" << cbFired.load()
                 << endl;
            failed++;
        }

        this_thread::sleep_for(10ms);
    }

    cout << endl << "=== Results: " << passed << " passed, " << failed << " failed ===" << endl;

    return failed > 0 ? 1 : 0;
}
