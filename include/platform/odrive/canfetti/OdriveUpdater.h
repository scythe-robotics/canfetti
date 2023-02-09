#pragma once
#include "Drivers/STM32/stm32_system.h"
#include "canfetti/ObjDict.h"
#include "canfetti/modules/FwUpdater.h"

class OdriveUpdater : public FwUpdater {
 public:
  void checkConfig();

 protected:
  static const uint32_t CopyConfigCookieVal = 0x01DBEEF;
  static uint32_t copyConfigCookie;

  bool reinit(PartitionInfo& info);
  void reboot();
  void resetState(bool start);
  canfetti::Error chunkStart(uint32_t dstAddr, uint32_t size);
  canfetti::Error chunkFinish(uint32_t dstAddr, uint8_t* data, uint32_t size, uint32_t& crcResult);
  canfetti::Error imageFinish();

 private:
  static constexpr const size_t ConfigNumSectors   = 2;   // num sectors reserved for config
  static constexpr const size_t SectorsPerBank     = 64;  // 512 kB version
  static constexpr const size_t WordSizeBytes      = 16;
  static constexpr const size_t SectorSize         = 8192;
  static constexpr const uint32_t FlashBank1Addr   = 0x8000000;
  static constexpr const uint32_t FlashBank2Addr   = 0x8100000;
  static constexpr const uint32_t FlashBank2Offset = 0x100000;

  canfetti::Error eraseSector(uint32_t sector, uint32_t num, uint32_t bank);

  uint8_t* buf                   = nullptr;
  uint32_t erasedSectorWatermark = 0;
  bool runningFromBank1          = true;
  FLASH_OBProgramInitTypeDef obCfg;
};
