#pragma once

#include <stdint.h>
#include <experimental/source_location>
#include <string>
#include <tuple>
#include "canfetti/System.h"

namespace canfetti {

namespace StaticDataTypeIndex {
constexpr std::tuple<uint16_t, uint8_t> Integer8   = std::make_tuple(0x0002, 0);
constexpr std::tuple<uint16_t, uint8_t> Integer16  = std::make_tuple(0x0003, 0);
constexpr std::tuple<uint16_t, uint8_t> Integer32  = std::make_tuple(0x0004, 0);
constexpr std::tuple<uint16_t, uint8_t> Unsigned8  = std::make_tuple(0x0005, 0);
constexpr std::tuple<uint16_t, uint8_t> Unsigned16 = std::make_tuple(0x0006, 0);
constexpr std::tuple<uint16_t, uint8_t> Unsigned32 = std::make_tuple(0x0007, 0);
constexpr std::tuple<uint16_t, uint8_t> Real32     = std::make_tuple(0x0008, 0);
}  // namespace StaticDataTypeIndex

enum State {
  Bootup         = 0x00,
  Stopped        = 0x04,
  Operational    = 0x05,
  PreOperational = 0x7f,
  Offline        = 0xff,  // Not part of spec.  Used only for uninitialized internal state
};

enum SlaveState {
  GoOperational    = 0x01,
  Stop             = 0x02,
  GoPreOperational = 0x80,
  ResetNode        = 0x81,
  ResetComms       = 0x82,
};

enum class Error {
  // Custom Errors
  Success       = 0,
  InternalError = 1,

  // CANopen Errors from spec
  NotToggled            = 0x05030000,  // Toggle bit not alternated.
  Timeout               = 0x05040000,  // SDO protocol timed out.
  InvalidCmd            = 0x05040001,  // Client/server command specifier not valid or unknown.
  InvalidBlkSize        = 0x05040002,  // Invalid block size (block mode only).
  InvalidSeqNum         = 0x05040003,  // Invalid sequence number (block mode only).
  CrcError              = 0x05040004,  // CRC error (block mode only).
  OutOfMemory           = 0x05040005,  // Out of memory
  UnsupportedAccess     = 0x06010000,  // Unsupported access to an object.
  ReadViolation         = 0x06010001,  // Attempt to read a write only object.
  WriteViolation        = 0x06010002,  // Attempt to write a read only object
  IndexNotFound         = 0x06020000,  // Object does not exist in the object dictionary.
  ObjMappingError       = 0x06040041,  // Object cannot be mapped to the PDO.
  PdoSizeViolation      = 0x06040042,  // The number and length of the objects to e mapped would exceed PDO length.
  ParamIncompatibility  = 0x06040043,  // General parameter incompatibility reason.
  DeviceIncompatibility = 0x06040047,  // General internal incompatibility in the device.
  HwError               = 0x06060000,  // Access failed due to a hardware error.
  ParamLength           = 0x06070010,  // Data type does not match; length of service parameter does not match.
  ParamLengthHigh       = 0x06070012,  // Data type does not match; length of service parameter too high.
  ParamLengthLow        = 0x06070013,  // Data type does not match; length of service parameter too low.
  InvalidSubIndex       = 0x06090011,  // Sub-index does not exist.
  ValueRange            = 0x06090030,  // Value range of parameter exceeded (only for write access).
  ValueRangeHigh        = 0x06090031,  // Value of parameter written too high.
  ValueRangeLow         = 0x06090032,  // Value of parameter written too low.
  MinMaxKerfuffle       = 0x06090036,  // Maximum value is less than minimum value.
  Error                 = 0x08000000,  // General error.
  DataXfer              = 0x08000020,  // Data cannot be transferred or stored to the application.
  DataXferLocal         = 0x08000021,  // Data cannot be transferred or stored to the application because of local control.
  DataXferState         = 0x08000022,  // Data cannot be transferred or stored to the application because of the present device state.
  OdGenFail             = 0x08000023,  // Object dictionary dynamic generation fails or no object dictionary is present (e.g. object dictionary is generated from file and generation fails because of a file error).
};

static constexpr const char* trimSlash(const char* const str, const char* const last_slash)
{
  return *str == '\0' ? last_slash : *str == '/' ? trimSlash(str + 1, str + 1)
                                                 : trimSlash(str + 1, last_slash);
}

static constexpr const char* trimSlash(const char* const str)
{
  return trimSlash(str, str);
}

template <typename... Args>
struct LogInfo {
  LogInfo(const char* fmt, Args&&... args, const std::experimental::source_location& loc = std::experimental::source_location::current())
  {
    const char* filename = trimSlash(loc.file_name());
    std::string s(fmt);
    if (Logger::needNewline) {
      s += "\n";
    }
    if constexpr (sizeof...(args) == 0) {
      Logger::logger.emitLogMessage(filename, loc.function_name(), loc.line(), "%s", s.c_str());
    }
    else {
      Logger::logger.emitLogMessage(filename, loc.function_name(), loc.line(), s.c_str(), args...);
    }
  }
};

template <typename... Ts>
LogInfo(const char* f, Ts&&...) -> LogInfo<Ts...>;

template <typename... Args>
struct LogDebug {
  LogDebug(const char* fmt, Args&&... args, const std::experimental::source_location& loc = std::experimental::source_location::current())
  {
    if (Logger::logger.debug) {
      const char* filename = trimSlash(loc.file_name());
      std::string s(fmt);
      if (Logger::needNewline) {
        s += "\n";
      }
      if constexpr (sizeof...(args) == 0) {
        Logger::logger.emitLogMessage(filename, loc.function_name(), loc.line(), "%s", s.c_str());
      }
      else {
        Logger::logger.emitLogMessage(filename, loc.function_name(), loc.line(), s.c_str(), args...);
      }
    }
  }
};

template <typename... Ts>
LogDebug(const char* f, Ts&&...) -> LogDebug<Ts...>;

struct Msg {
  uint32_t id;
  bool rtr;
  uint8_t len;
  uint8_t* data;

  inline uint16_t getFunction() const { return id & (0xF << 7); }
  inline uint8_t getNode() const { return id & ((1 << 7) - 1); }
};

}  // namespace canfetti
