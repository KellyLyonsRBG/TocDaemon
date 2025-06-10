/******************************************************************************
 *
 * \file TocDaemonServer.cpp
 * \brief Base class for spawning Aqueti submodules. Also provides 
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

#include "TocDaemonServer.h"
#include <fstream>
#include <nlohmann/json.hpp>

TocDaemonServer::TocDaemonServer(const nlohmann::json& config)
    : m_ok(true), m_config(config) {
    // Use the config as needed
}

TocDaemonServer::TocDaemonServer(const std::string& configFile, const std::string& logFile)
    : m_ok(true) {
    // Load config from file using nlohmann::json
    std::ifstream in(configFile);
    if (in) {
        in >> m_config;
    } else {
        m_ok = false;
    }
}

TocDaemonServer::~TocDaemonServer() {}

bool TocDaemonServer::doingOkay() const {
    return m_ok;
}
