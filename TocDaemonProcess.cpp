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
#include "TocDaemonClient.h"
#include <iostream>

#include <signal.h>
#include <stdio.h>
#ifndef _WIN32
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/time.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <iostream>
#include <memory>
#include <thread>

//#include "acl/FileIO.h"
//#include "AqtVersion.hpp"
//#include "atl/Heartbeat/AtlEKG.tcc"
//#include "atl/Heartbeat/AtlHeartbeat.hpp"

using namespace std;

#define DAEMONPROCESS_LOCKFILE "/var/lock/DaemonProcess.lock"

typedef enum {
    ADP_STANDARD    = 0,
    ADP_SERVER_TEST = 1,
    ADP_CLIENT_TEST = 2
} ADP_OPERATING_MODE;

ADP_OPERATING_MODE g_mode = ADP_STANDARD;

bool doingOkay = true;
AquetiDaemonServer* tds;
bool daemonized = false;
std::string configFile = "/etc/toc/daemonConfiguration.json";
std::string logfile = "/var/log/toc/TocDaemon.log";
std::string shutdownfilePath = "/var/log/toc/__shutdown__.tmp";
FILE* fOUT = NULL;
FILE* fERR = NULL;


void printVersion()
{
    cout << "TOC daemon v-" << toc::AqtVersion::getAqtVersionString() << endl
         << "git commit hash - " << toc::AqtVersion::getAqtCommitHash() << endl;
}


bool printHelp( void )
{
    cout << "AquetiDaemonProcess - Main process that spawns and maintains submodules\n" << endl;
    cout << "Usage: ./AquetiDaemonProcess " << endl;
    cout << "Options:" << endl
         << "\t-h   prints this help message and exits" << endl
         << "\t-d   flag to indicate we are running as a system daemon" << endl
         << "\t--mode [standard, serverTest, clientTest] set the mode" << endl
            << "of operation for the process (default=standard)" << endl
         << "\t--version   prints version information and exits" << endl
         << endl;
    return 1;
}


static void handleDaemonFlatline(std::string daemon)
{
    std::cout << "TocDaemonProcess: daemon failed to pulse restarting" << std::endl;
    doingOkay = false;
}


static void sigHandler(int x)
{
    std::cout << "TocDaemonProcess received shutdown signal" << std::endl;
    doingOkay = false;
}


void mainLoop()
{   
	do
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		//TODO emit a heartbeat to systemD
	} while(doingOkay && tds->doingOkay());
	if (!tds->doingOkay()) {
        std::cout << "TocDaemonProcess exiting" << std::endl;
        //sleep for a few seconds to prevent rapid restarts that could anger systemd
        std::this_thread::sleep_for(std::chrono::seconds(5)); 
        std::exit(1);
    }
    std::cout << "DaemonProcess shutting down" << std::endl;
    delete tds;
}


void initStandard()
{
    // if running as a process, redirect stdout to out log file
    if (!daemonized) {
        std::cout << "Redirecting stderr to file: " << logfile << std::endl;
        fERR = freopen(logfile.c_str(), "a", stderr);
        if (!fERR) {
#ifndef _WIN32
            stderr = fdopen(1, "a");
#endif
            std::cout << "Failed to open log file for stderr redirection" << std::endl;
        }
        std::cout << "Redirecting stdout to file: " << logfile << std::endl;
        fOUT = freopen(logfile.c_str(), "a", stdout);
        if (!fOUT) {
#ifndef _WIN32
            stdout = fdopen(1, "a");
#endif
            std::cout << "Failed to open log file for stdout redirection" << std::endl;
        }
    }

    // print startup message
    cout << "================================================================" << endl
         << "    Initializing AquetiDaemonProcess v" << tds::AqtVersion::getAqtVersionString() << " commit: " << aqt::AqtVersion::getAqtCommitHash() << endl
         << "================================================================" << endl;
	tds = new TocDaemonServer(configFile, (daemonized ? "" : logfile));
}


void initServerTest()
{
    //JsonBox::Value serverConf;
    //serverConf["directoryOfServices"]["type"].setString("server");
    serverConf["directoryOfServices"]["system"].setString("adp_test");

    // print startup message
    cout << "================================================================" << endl
         << "    Initializing AquetiDaemonProcess v" << aqt::AqtVersion::getAqtVersionString() << " commit: " << aqt::AqtVersion::getAqtCommitHash() << endl
         << endl
         << "    Running server test with system name: " << serverConf["directoryOfServices"]["system"].getString() << endl
         << "================================================================" << endl;

    tds = new AquetiDaemonServer(serverConf);
}


void initClientTest()
{
    //JsonBox::Value serverConf;
    //serverConf["directoryOfServices"]["type"].setString("client");
    serverConf["directoryOfServices"]["system"].setString("adp_test");

    // print startup message
    cout << "================================================================" << endl
         << "    Initializing AquetiDaemonProcess v" << aqt::AqtVersion::getAqtVersionString() << " commit: " << aqt::AqtVersion::getAqtCommitHash() << endl
         << endl
         << "    Running client test with system name: " << serverConf["directoryOfServices"]["system"].getString() << endl
         << "================================================================" << endl;

    tds = new AquetiDaemonServer(serverConf);
}


int main(int argc, char** argv)
{
	for(int i =1; i < argc; i++){
		if( !strcmp(argv[i], "-h" )){
			printHelp();
			return EXIT_SUCCESS;
		} else if (!strcmp(argv[i], "-d")) {
            daemonized = true;
        } else if (!strcmp(argv[i], "--version")) {
            printVersion();
            return EXIT_SUCCESS;
        } else if (!strcmp(argv[i], "--mode")) {
            if (++i >= argc) {
                std::cout << "--mode option must specify a mode" << std::endl;
                printHelp();
                return EXIT_FAILURE;
            }
            if (!strcmp(argv[i], "standard"))
                g_mode = ADP_STANDARD;
            else if (!strcmp(argv[i], "serverTest"))
                g_mode = ADP_SERVER_TEST;
            else if (!strcmp(argv[i], "clientTest"))
                g_mode = ADP_CLIENT_TEST;
            else {
                std::cout << "Unrecognized mode: " << argv[i] << std::endl;
                printHelp();
                return EXIT_FAILURE;
            }
        } else {
            cerr << "ERROR: Unrecognized argument" << endl;
            printHelp();
            return EXIT_FAILURE;
        }
	}

#ifndef _WIN32
    // set umask of process
    umask(S_IWOTH);

	// lock lockfile to ensure no duplicate processes
    int lockfd = open(AQUETIDAEMONPROCESS_LOCKFILE, O_RDWR | O_CREAT, 0666);
    if (lockfd < 0) {
        perror("ERROR: Failed to open lockfile " AQUETIDAEMONPROCESS_LOCKFILE);
        return EXIT_FAILURE;
    }
    errno = 0;
    int rc = flock(lockfd, LOCK_EX | LOCK_NB);
    if (rc != 0) {
        if (errno == EWOULDBLOCK) {
            std::cerr << "ERROR: Failed to lock lockfile " << AQUETIDAEMONPROCESS_LOCKFILE
                      << ", this probably means another AquetiDaemonProcess is running"
                      << std::endl;
        } else {
            perror("ERROR: Failed to lock lockfile " AQUETIDAEMONPROCESS_LOCKFILE);
        }
        return EXIT_FAILURE;
    }
#else
    std::cerr << "Warning: Not ensuring that we are not already running; not implemented on Windows"
        << std::endl;
#endif

	// initialize signal handlers and heartbeat monitor
	signal(SIGINT,sigHandler);
	signal(SIGTERM,sigHandler);
    atl::AtlEKG<std::string>* EKG = new atl::AtlEKG<std::string>(handleDaemonFlatline);


    // set file descriptor soft limit to hard limit
#ifndef _WIN32
    struct rlimit fdLim;
    rc = getrlimit(RLIMIT_NOFILE, &fdLim);
    if (rc != 0) {
        perror("ERROR: Failed to get process file descriptor limits: ");
        return EXIT_FAILURE;
    } else if (fdLim.rlim_max < 1024) {
        std::cerr << "ERROR: Hard limit on file descriptors is "
                << fdLim.rlim_max << " < 1024" << std::endl;
        return EXIT_FAILURE;
    } else {
        fdLim.rlim_cur = fdLim.rlim_max;
        rc = setrlimit(RLIMIT_NOFILE, &fdLim);
        if (rc != 0) {
            perror("ERROR: Failed to set process file descriptor softlimit: ");
            return EXIT_FAILURE;
        }
    }
#endif

/*
    // check ghettofab detector file. If we find it, delete it and force a reboot
    std::fstream shutdownfile;
    if (daemonized && atl::filesystem::exists(shutdownfilePath)) {
        std::cerr << "Detected bad shutdown from last instance. "
                  << "Forcing restart of AquetiDaemon" << std::endl;
        goto cleanup_and_shutdown;
    }

    // else we make the file and start doin stuff
    shutdownfile.open(shutdownfilePath, ios::out);
    shutdownfile.close();
*/

    // check our operating mode and initialize the AquetiDaemonServer
    switch (g_mode) {
        case ADP_STANDARD : 
            initStandard();
            break;
        case ADP_SERVER_TEST :
            std::cout << "Daemon Process initializing in server test mode" << std::endl;
            initServerTest();
            break;
        case ADP_CLIENT_TEST :
            std::cout << "Daemon Process initializing in client test mode" << std::endl;
            initClientTest();
            break;
    }

    // set up heartbeat for the AquetiDaemonServer and start mainLoop
    EKG->addHeartbeat(std::string("daemon"),(atl::Heartbeat*)tds);
    mainLoop();


//cleanup_and_shutdown:
    delete EKG;

    // delete ghettofab detector file before exit
    acl::filesystem::remove(shutdownfilePath);

    return EXIT_SUCCESS;
}
