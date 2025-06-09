/******************************************************************************
 *
 * \file AquetiDaemonServer.hpp
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
#pragma once

//system headers
#ifndef _WIN32
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/timex.h>
#endif
#include <sys/types.h>
#include <iostream>
#include <fstream>
#include <string>
#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <linux/if_link.h>
#endif

#include "acl/RandomIDGenerator.hpp"
#include "acl/Thread.h"
#include "acl/ThreadWorker.h"
#include "acl/TSMap.tcc"
#include "apl/BaseContainer.h"

#include "atl/Property/SingleLinkPropertyManager.h"
#include "atl/Property/StateVector.tcc"
#include "atl/Heartbeat/AtlEKG.tcc"
#include "atl/Heartbeat/AtlHeartbeat.hpp"
#include "atl/Avahi/AvahiBroadcast.hpp"
#include "acl/Timer.h"
#include "atl/PipelineInterfaces/SocketContainerReceiver.h"
#include "atl/Logger/LumberMillServer.hpp"
#include "atl/Logger/LumberMillClient.hpp"
#include "atl/Logger/LumberjackServer.hpp"
#include "atl/Logger/LumberjackClient.hpp"
#include "atl/Logger/BaseLogger.hpp"

#include "module/ClientSubmodule.hpp"
#include "module/RemoteSubmodule.hpp"
#include "module/ServerSubmodule.hpp"
#include "module/SubmoduleDescriptor.hpp"

//data headers
#include "data/DataManagers/ContainerCache.h"
#include "data/MnemosyneServer.hpp"
#include "data/MnemosyneClient.hpp"
#include "data/CoeusServer.hpp"
#include "data/CoeusClient.hpp"
#include "data/DataRequestRouter.hpp"

#ifndef TEGRA
//agt headers
#include "render/HyperionServer.hpp"
#include "render/HyperionClient.hpp"
#include "render/CronusServer.hpp"
#include "render/CronusClient.hpp"
#include "modelGen/ModelHandlerServer.hpp"
#include "import-export/ImportExportControlServer.hpp"
#endif

//daemon headers
#include "AquetiDaemonInterface.hpp"
#include "DirectoryServices.hpp"
#include "DirectoryServicesServer.hpp"
#include "DirectoryServicesClient.hpp"

#define DAEMON_HEARTBEAT_TIMEOUT 30.0 //seconds
#define ID_PATH "/etc/aqueti/guid"
#define NTP_OFFSET_SAMPLE_WINDOW 200

class AquetiDaemonServer : public atl::AquetiDaemonInterface,
                           public acl::Thread,
                           public atl::Heartbeat,
                           public aqt::BaseLogger
{
    public:
        /**
         * @brief Constructor, this constructor loads the configuration from a file
         **/
        AquetiDaemonServer(std::string configFile="/etc/aqueti/daemonConfiguration.json"
                , std::string logfile="");

        /**
         * @brief Constructor, this constructor takes the configuration from an arguement,
         *         convenient for testing
         **/
        AquetiDaemonServer(JsonBox::Value configuration, std::string logfile="");

        virtual ~AquetiDaemonServer();

        atl::PropertyManagerCreated getConnectionStruct(){ return m_connectionStruct; }

        std::string getHostId() { return m_id; }
        std::string getSystemId() { return m_systemId; }

        /** 
         * @brief Overrides atl::Heartbeat doingOkay() so we can add additional
         *      "okay" parameters other than heartbeat timeout
         * @return bool
         **/
        bool doingOkay();

        /** 
         * @brief sets the flag to indicate a critical failure state of this top-level object,
         *      so that main() will know to exit and allow systemd to restart the process
         * @return true on success, else false
         **/
        bool setCriticalFailureFlag();

        /***********************
         * Submodule management
         ***********************/
        /**
         * @brief Returns a pointer to the submodule corresponding to the given ID.
         *      If the submodule was locally constructed by this AquetiDaemonServer, this 
         *      method returns a pointer (TODO shared pointer?) to the constructed object.
         *      Else, it constructs a client interface to the remote object and returns
         *      the constructed client instead. TODO These constructed clients may be
         *      shared by multiple local submodules.
         * @param submoduleID The ID of the desired submodule
         * @return A pointer to an abstract submodule interface class, which may be
         *      a local submodule or constructed client interface to a remote submodule
         **/
        std::shared_ptr<aqt::BaseSubmodule> getSubmodule(aqt::SubmoduleDescriptor submoduleID);
       
        /**
         * @brief This method enable submodules to emplace new submodules and have them managed by the
         *      daemon and directory of services system. When this method is called, the daemon will
         *      emplace the shared pointer in the m_knownSubmodules map and then call a method on the directory
         *      of services so that remote submodules can create a remote interface to the added submodule.
         * @param submoduleID The identifier struct for the added submodule
         * @param submodule ServerSubmodule shared pointer to the constructed submodule
         *
         * @return will return true if the pointer is successfully emplaced into the daemons map
         **/
        bool addSubmodule(std::shared_ptr<aqt::BaseSubmodule> submodule);
        
        /**
         * @brief      Gets the daemon resource.
         *
         * @param[in]  ID   the string identifier for the daemon resource
         *
         * @return     The daemon resource.
         */
        std::shared_ptr<aqt::BaseDaemonResource> getDaemonResource(std::string ID);

        /**
         * @brief Sets a callback to be called whenever a new submodule is discovered
         *      anywhere in the system. This callback can be optionally filtered by the
         *      type of submodule.
         *      
         * @param m_id the unique identifier for [this] calling this method     
         * @param callback The callback function to call when a new submodule is discovered.
         *      The callback's argument will be the ID of the new submodule, and returns void.
         * @param submoduleType An optional filter parameter to specify a type of submodule to notify about
         **/

        bool setNewSubmoduleCallback(std::function<void(aqt::SubmoduleDescriptor)> callback, std::string yourid, std::string type="");

        /**
         * @brief Sets a callback to be called whenever a submodule is deleted
         *      This callback can be optionally filtered by the
         *      type of submodule.
         *      
         * @param m_id the unique identifier for [this] calling this method     
         * @param callback The callback function to call when a new submodule is discovered.
         *      The callback's argument will be the ID of the new submodule, and returns void.
         * @param submoduleType An optional filter parameter to specify a type of submodule to notify about
         **/
        bool setDeletedSubmoduleCallback(std::function<void(aqt::SubmoduleDescriptor)> callback, std::string yourid, std::string type="");

        /**
         * @brief Gets a list of all submodules in the system, both local and remote
         * @param submoduleType An optional type parameter to filte rthe return list. 
         *      An empty string does no filtering.
         * @return A vector of submodule IDs
         **/
        std::vector<aqt::SubmoduleDescriptor> getSubmoduleList(std::string submoduleType="");

        /**
         * @brief      Gets the daemon resource list.
         *
         * @return     The daemon resource list.
         */
        std::vector<std::string> getDaemonResourceList();


        /**
         * Local Hardware configuration stuff
         */
        atl::DatabaseInfo getGlobalDatabaseInfo(){ return m_globalDB; };
        atl::DatabaseInfo getLocalDatabaseInfo(){ return m_localDB; };

    	/**
    	 * @brief Returns JSON status of this object or a subobject
    	 * @param [in] entity Subentity identifier string. If empty, query is on 
    	 *      this, else it specifies path to subobject for status query
    	 * @param [out] status 
    	 *      ::AQT_STATUS_SUCCESS on success
    	 *      ::AQT_STATUS_INVALID_VALUE on bad entity id
    	 * @return JsonBox value of status empty on error
    	 **/
    	JsonBox::Value getDetailedStatus(const std::string &entity
    	        , aqt::aqt_Status *status=nullptr);

    	/**
    	 * @brief Returns JSON of current values of settable parameters
    	 * @param [in] entity Subentity identifier, this object if empty
    	 * @param [out] status
    	 * ::AQT_STATUS_SUCCESS on success
    	 * ::AQT_STATUS_INVALID_VALUE on bad entity id
    	 * \return JsonBox::Value result of query, empty on error
    	 *
    	 * this provides results for the api call of the same name
    	 *
    	 * parameters are a subset of status
    	 * the return of this method can be used as input to SetParameters
    	 *
    	 **/
    	JsonBox::Value getParameters(const std::string &entity
    	        , aqt::aqt_Status *status=nullptr);
    
    	/**
    	*  \brief set parameters via json
    	*  \param [in] entity subentity identifier, this object if empty
    	*  \param [in] params json of parameters to be set
    	*  \param [out] status
    	*  ::AQT_STATUS_SUCCESS on success
    	*  ::AQT_STATUS_INVALID_VALUE on bad entity id
    	*  \return jsonbox value of results
    	*  atomicity is not guaranteed
    	*  implementations may do basic checking before setting parameters
    	*  some parameters may be set in an error case
    	*
    	*  the results string should be accurate even in error
    	*
    	**/
    	JsonBox::Value setParameters(const std::string &entity
    	        , const JsonBox::Value &params
    	        , aqt::aqt_Status *status=nullptr);


        /**
         * \brief updates the configuration of the given submodule with the specified JSON
         * \return success or failure
         **/
        bool updateSubmoduleConfig(aqt::SubmoduleDescriptor sd, JsonBox::Value val);
        /**
         * @brief Causes this daemon to prepare for a software update
         *      by opening a socket container receiver.
         * @param [in] numberOfPackages the number of packages expected to
         *      install
         * 
         * @return Port that the container receiver was opened on.
         **/
    	int prepareDaemonForSoftwareUpdate(int numberOfPackges);

        //TODO update this method to accept a vector of hashes in the case that their
        //are multiple packages to install
        /**
         * @brief Cause the daemon to remove all of it's shared pointers from the map
         *      causing the destruction of the submodules. It then dequeues the packages
         *      off the queue, writes them to a file and invokes a system call to dpkg 
         *      them.
         * @param [in] packageHash the SHA256 hash of the binary in the package
         *
         * @return returns 0 on success, defined return codes otherwise.
         **/
        int updateDaemonSoftwareandRestart(std::string packageHash);

    private:	
        void initializeLogging(std::string logfile="");
        void initialize();
        /**
         * @brief Thread main loop
        **/
        void mainLoop();

        /**
         * Unique ID of the device this object is running on
         **/
        std::string m_id;
        std::string m_systemId;

        /**
         * @brief Submodule heartbeat monitor
         **/
        atl::AtlEKG<aqt::SubmoduleDescriptor> m_serverEKG;

        /**
         * @brief Timing object
         **/
        acl::Timer m_runTimer;

        //TODO in the future we may also want a client submodule EKG
        /**
         * Connectivity stuff
         */
        uint16_t m_port = 0;

        /**
         * Objects and methods for software updates
         */
        acl::TSQueue<apl::BaseContainer> m_packageContainers;
        atl::SocketContainerReceiver* m_updateReceiver = nullptr;

        /**
         * Process control stuff
         */
        // TODO there is now no reason for these to be separate methods
        std::shared_ptr<aqt::ClientSubmodule> createClientFromRemote(aqt::SubmoduleDescriptor submod,
                atl::PropertyManagerCreated connStruct);

        bool createNewSubmodule(JsonBox::Value submodule);
        bool initSubmodulesFromConfig();
        //bool initDaemonResources();
        bool createNewDaemonResource(std::string ID);
        std::recursive_mutex m_resourceMutex;
        // TODO update this method to reflect changes to ACI to be a submodule like the others
        bool createNewCaptureModule(std::string compressionMode, int scales, std::string baseResolution);
        DirectoryServices* m_directoryOfServices = nullptr;
#ifndef _WIN32
        atl::AvahiBroadcast* m_AvahiBroadcast;
#endif
		std::string m_avahiBroadcastName;
        std::shared_ptr<aqt::ServerSubmodule> m_lms = nullptr;
        /**
         * Map of submodules indexed by submoduleID string
         */
        acl::TSMap<aqt::SubmoduleDescriptor,std::shared_ptr<aqt::ServerSubmodule>> m_activeSubmodules;
        acl::TSMap<aqt::SubmoduleDescriptor,std::shared_ptr<aqt::ClientSubmodule>> m_clientSubmodules;
        /**
         * TSMap of Daemon resources
         */
        acl::TSMap<std::string,std::shared_ptr<aqt::BaseDaemonResource>> m_daemonResources;
        /**
         * Map of callback function pointers indexed by submoduleType
         */
        // TODO put the type and submodule key in a struct and use that instead
        acl::TSMap<aqt::SubmoduleDescriptor,std::function<void(aqt::SubmoduleDescriptor)>> m_callbackFunctions;
        acl::TSMap<aqt::SubmoduleDescriptor,std::function<void(aqt::SubmoduleDescriptor)>> m_deletedFunctions;
        /**
        * @brief Callback functions that the daemon passes to its constructed directory of services that get called when a
        *      submodule is created or destroyed. It basically examines the type of submodule that has come online,
        *      if a local submodule is bound to this type, the daemon will construct a client interface and pass it to the 
        *      bound submodule
        **/

        void handleCreatedSubmodule(aqt::SubmoduleDescriptor submodule, atl::PropertyManagerCreated connStruct);
        void handleDestroyedSubmodule(aqt::SubmoduleDescriptor submodule);

        /**
         * @brief this method is called via directive on any non DOSserver daemons to initialize
         *      their DOS clients
         **/
        bool initDirectoryOfServicesClient(atl::PropertyManagerCreated connStruct);
        /**
         * @brief Given a new submodule and it's connection information, this method will examine if any of the local modules
         *      care about it's type if so it will construct the remote interface and add it to m_clientSubmodules
         **/
        void alertCallbackFunctions(aqt::SubmoduleDescriptor submoduleType); 
        void alertDeletedCallbackFunctions(aqt::SubmoduleDescriptor submoduleType); 
        
        /**
         * Property stuff
         */
        bool initPropertyManagerServer();
        atl::PropertyManagerCreated getSubmoduleConnStruct(aqt::SubmoduleDescriptor submoduleID);

        atl::Property<int> m_maxStorage;
        atl::Property<int> m_maxRender;
        atl::Property<int> m_maxCapture;

        atl::Property<std::string> m_idString;
        atl::Property<atl::PropertyManagerCreated> m_dosConn;

        atl::SingleLinkPropertyManager m_daemonManager;
        atl::PropertyManagerCreated m_connectionStruct;
        atl::Directive<aqt::SubmoduleDescriptor,atl::PropertyManagerCreated> m_getSubModuleConnectionStruct;
        atl::Directive<std::string,atl::StateVector<aqt::SubmoduleDescriptor>> m_getSubmoduleList;
        atl::Directive<atl::PropertyManagerCreated,bool> m_initDirectoryServices;
        atl::Directive<std::string,std::string> m_getDetailedStatus;
        atl::Directive<std::string,std::string> m_getParameters;
        atl::Directive<std::string,std::string> m_setParameters;
        atl::Directive<int,int> m_prepareSoftwareUpdate;
        atl::Directive<std::string,int> m_softwareUpdate;
        
        /**
         * @brief Database Structs
         **/
        atl::DatabaseInfo m_localDB = {"127.0.0.1:27017","acos"};
        atl::DatabaseInfo m_globalDB = {"127.0.0.1:27017","acos"};

        /**
         * @brief Hardware query objects and methods
         **/
#ifndef _WIN32
        timex m_timex;
#endif
        void getUpdatedClockInfo();
        JsonBox::Value getNTPSettings();
        bool isNTPServer();
        JsonBox::Array getNetworkJson();
        void updateInterfaceIpNetmask(std::string iface, std::string ip, std::string netmask);
        void addNtpServer(std::vector<std::string> servers);
        //Bool if set to true causes the aqueti daemon server to throw a sigterm
        bool m_restart=false;

        // JSON API helper functions
        std::vector<double> m_NTPOffset;
        double m_offset = 0;
        double getNTPoffset();
        JsonBox::Value getRamStatusJson();
        JsonBox::Value getCpuStatusJson();
        JsonBox::Value getGpuStatusJson();
        JsonBox::Value getDriveStatusJson();
        JsonBox::Value getSystemStateJson(JsonBox::Value& detailedStatus);
        void forceNtpSync();
        void restartSystem();
        void shutdownSystem();

        /**
         * @brief Configuration objects and methods
         **/
        std::string m_configFile = "";
        JsonBox::Value m_configuration = {};
        std::mutex m_configMutex;
        bool updateConfigFile(JsonBox::Value config);

		int m_loopCount =0;
		JsonBox::Value m_broadcastParams;

        std::atomic<bool> m_destructorFlag;
        std::mutex m_destructorMutex;

        // independent thread to handle heartbeat
        acl::ThreadWorker m_pulsar;

        // status variable
        std::atomic<bool> m_doingOkay;
};

