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
#include <iostream>
#include <chrono>

TocDaemonServer::TocDaemonServer(const nlohmann::json& config)
    : Heartbeat(5.0), m_ok(true), m_config(config) {
    initPropertyManagerServer();
    initSubmodulesFromConfig();
    startPulsar();
}

TocDaemonServer::TocDaemonServer(const std::string& configFile, const std::string& logFile)
    : Heartbeat(5.0), m_ok(true) {
    std::ifstream in(configFile);
    if (in) {
        in >> m_config;
    } else {
        m_ok = false;
    }
    initPropertyManagerServer();
    initSubmodulesFromConfig();
    startPulsar();
}

TocDaemonServer::~TocDaemonServer() {
    stopPulsar();
}

bool TocDaemonServer::doingOkay() {
    return m_ok && Heartbeat::doingOkay();
}

void TocDaemonServer::startPulsar() {
    m_pulsarRunning = true;
    m_pulsar = std::thread([this]() {
        while (m_pulsarRunning) {
            this->pulse();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
}

void TocDaemonServer::stopPulsar() {
    m_pulsarRunning = false;
    if (m_pulsar.joinable()) m_pulsar.join();
}

void TocDaemonServer::initSubmodulesFromConfig() {
    std::cout << "[TocDaemonServer] Initializing submodules from config..." << std::endl;
}

void TocDaemonServer::initPropertyManagerServer() {
    std::cout << "[TocDaemonServer] Initializing property manager server..." << std::endl;
}
