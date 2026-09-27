// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef PPRZLINKCPP_XBEEMODEM_H
#define PPRZLINKCPP_XBEEMODEM_H

#include <pprzlink/Device.h>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace pprzlink {
  struct XbeeConfiguration {
    uint16_t localAddress = 0x100;
    std::optional<uint8_t> channel; // Unchanged unless specified; range 0x0c..0x17.
    std::chrono::milliseconds guardTime{2000};
    std::chrono::milliseconds responseTimeout{2000};
    /// Detect the modem's baud rate, switch to this standard rate and save changes.
    /// Requires a SerialDevice. nullopt keeps the existing baud rate without probing.
    std::optional<unsigned int> targetBaudrate{57600};
  };

  struct XbeeBaudrateInfo {
    unsigned int detected;
    unsigned int configured;
    bool savedToFlash;
  };

  /** Configure an 802.15.4 modem through UART AT commands.
   * Exclusively use the device during initialization; service its I/O loop and poll
   * regularly. No sleeps or owned thread. The default escape character '+' is assumed.
   * Writes use Device's synchronous contract; response deadlines do not bound writes.
   * ATWR saves all current settings, before this initializer changes MY/CH/AP.
   */
  class XbeeModem {
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit XbeeModem(const XbeeConfiguration &configuration, TimePoint now = Clock::now());

    /// Returns true only after all replies, baud verification/save and final ATCN.
    /// Errors leave sends blocked. Only missing entry replies advance to another baud.
    /// Optional timestamps use steady_clock and allow deterministic timer tests.
    bool poll(Device &device, TimePoint now = Clock::now());
    bool isReady() const noexcept { return stage == Stage::Ready; }
    /// Available after successful initialization with baud detection enabled.
    std::optional<XbeeBaudrateInfo> getBaudrateInfo() const noexcept;
    /// Preserve API bytes received in the same serial read as the final OK.
    BytesBuffer takeRemainingBytes();

  private:
    enum class Stage { GuardBefore, GuardAfter, Command, Ready, Failed };
    enum class Command { Configure, ReadBaudrate, SetBaudrate, ApplyBaudrate,
                         VerifyBaudrate, SaveBaudrate };
    void beginBaudrate(Device &device, unsigned int baudrate, TimePoint now);
    void sendCommand(Device &device, TimePoint now, Command next = Command::Configure);
    bool finishCommand(Device &device, TimePoint now, const std::string &line);
    std::optional<std::string> readReply(const BytesBuffer &bytes, bool entering, size_t &consumed);
    [[noreturn]] void fail(const std::string &reason);
    std::string operation() const;

    XbeeConfiguration configuration;
    std::vector<std::string> commands;
    std::vector<unsigned int> baudrates;
    size_t baudrateIndex = 0;
    unsigned int currentBaudrate = 0;
    bool verifyingBaudrate = false;
    std::optional<XbeeBaudrateInfo> baudrateInfo;
    size_t commandIndex = 0;
    Command command = Command::Configure;
    std::string pendingCommand;
    Stage stage = Stage::GuardBefore;
    TimePoint guardDeadline;
    TimePoint responseDeadline;
    bool entered = false;
    bool replyHasBinary = false;
    std::string reply;
    std::string failure;
    BytesBuffer remainingBytes;
  };
}
#endif // PPRZLINKCPP_XBEEMODEM_H
