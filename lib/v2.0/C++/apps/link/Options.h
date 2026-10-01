// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <expected>
#include <optional>
#include <string>

namespace link_app {
  struct Options {
    std::string ivyBus;
    std::string device = "/dev/ttyUSB0";
    std::string baudrate = "9600";
    std::string transport = "pprz";
    std::string broadcastAddress;
    bool trafficStatistics = false;
    bool aircraftInfo = true;
    bool uplink = true;
    bool hardwareFlowControl = false;
    bool localTimestamp = false;
    bool udp = false;
    bool udpBroadcast = false;
    bool xbee868 = false;
    bool redundantLink = false;
    bool help = false;
    int udpPort = 4242;
    int udpUplinkPort = 4243;
    int xbeeAddress = 0x100;
    int xbeeRetries = 10;
    int linkId = -1;
    int statusPeriod = 1000;
    int pingPeriod = 5000;
    int aircraftTimeout = 5000;
    std::optional<int> channel;
    std::optional<std::string> socatAction;

    Options();
    static std::expected<Options, std::string> parse(int argc, char **argv);
    static std::string usage();
    static std::string messagesPath();
    /// Semantic validation before opening any bus or device.
    void validate() const;
  };
}
