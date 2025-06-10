/******************************************************************************
 *
 * \file AquetiDaemonProcess.cpp
 * \author Andrew Ferg
 * \brief Primary process to run all AquetiOS submodules
 *
 * Copyright Aqueti 2018
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 *****************************************************************************/
#include "TocDaemonServer.h"
#include "Heartbeat/tocHeartbeat.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cerrno>
#ifndef _WIN32
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace std;

TocDaemonServer* tds = nullptr;
bool doingOkay = true;
bool daemonized = false;
std::string configFile = "config.json";


#ifndef _WIN32
#define DAEMONPROCESS_LOCKFILE "/var/lock/DaemonProcess.lock"
#endif

void mainLoop(Heartbeat& hb) {
    do {
        hb.pulse();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } while (doingOkay && tds && tds->doingOkay() && hb.doingOkay());
    if (!hb.doingOkay()) {
        std::cerr << "WARNING: Heartbeat missed!" << std::endl;
    }
    std::cout << "DaemonProcess shutting down" << std::endl;
    delete tds;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h")) {
            std::cout << "Usage: TocDaemonProcess [-h] [-d]" << std::endl;
            return 0;
        } else if (!strcmp(argv[i], "-d")) {
            daemonized = true;
        } else {
            std::cerr << "ERROR: Unrecognized argument: " << argv[i] << std::endl;
            return 1;
        }
    }
#ifndef _WIN32
    // set umask of process
    umask(S_IWOTH);

    // lock lockfile to ensure no duplicate processes
    int lockfd = open(DAEMONPROCESS_LOCKFILE, O_RDWR | O_CREAT, 0666);
    if (lockfd < 0) {
        perror("ERROR: Failed to open lockfile " DAEMONPROCESS_LOCKFILE);
        return EXIT_FAILURE;
    }
    errno = 0;
    int rc = flock(lockfd, LOCK_EX | LOCK_NB);
    if (rc != 0) {
        if (errno == EWOULDBLOCK) {
            std::cerr << "ERROR: Failed to lock lockfile " << DAEMONPROCESS_LOCKFILE
                      << ", this probably means another TocDaemonProcess is running"
                      << std::endl;
        } else {
            perror("ERROR: Failed to lock lockfile " DAEMONPROCESS_LOCKFILE);
        }
        return EXIT_FAILURE;
    }
#endif
    Heartbeat hb(5.0); // 5 second heartbeat timeout
    tds = new TocDaemonServer(configFile, "");
    mainLoop(hb);
    return 0;
}
