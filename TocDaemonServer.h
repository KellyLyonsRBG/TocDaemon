/******************************************************************************
 *
 * \file TocDaemonServer.hpp
 * \brief Base class for spawning Toc submodules. Also provides 
 *      network-accessible features via PropertyManager for querying/setting
 *      device-level parameters such as NTP configuration, network settings,
 *      and CPU/GPU usage statustics.
 * \author Bryan David Maione
 *
 * Copyright Aqueti 2018
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 *****************************************************************************/
#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <thread>
#include "Heartbeat/tocHeartbeat.h"

class TocDaemonServer : public Heartbeat {
public:
    TocDaemonServer(const nlohmann::json& config);
    TocDaemonServer(const std::string& configFile, const std::string& logFile = "");
    ~TocDaemonServer();
    bool doingOkay();
    void startPulsar();
    void stopPulsar();
    void initSubmodulesFromConfig();
    void initPropertyManagerServer();
private:
    bool m_ok = true;
    nlohmann::json m_config;
    std::thread m_pulsar;
    bool m_pulsarRunning = false;
};

