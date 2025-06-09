/******************************************************************************
 *
 * \file AquetiDaemonServer.cpp
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
//#include "CameraInterface/SCOPController/SCOPController.h"
//#include "CameraInterface/SCOPController/SCOPControllerClient.h"
//#include "CameraInterface/SCOPController/SCOPControllerInterface.h"
//#include "CameraInterface/MCAM/MicroCameraManager.h"
//#include "CameraInterface/MCAM/MicroCameraManagerClient.h"


TocDaemonServer::TocDaemonServer(std::string configFile, std::string logfile)
    : atl::Heartbeat(DAEMON_HEARTBEAT_TIMEOUT),
    m_serverEKG(nullptr),
    m_daemonManager("AquetiDaemonServer"),
    m_destructorFlag(false)
{
    // init atomics
    m_doingOkay = true;

    initializeLogging(logfile);

    m_configFile = configFile;
    // load configuration file and call other constructor
    try {
        m_configuration.loadFromFile(configFile);
        LOG_DEBUG(m_id,"Successfully loaded configration file");
    } catch (std::exception& e){
        LOG_CRITICAL(m_id, "Loading configuration generated exception, using empty config");
        m_configuration = "{}";
    }

    initialize();
}

AquetiDaemonServer::AquetiDaemonServer(JsonBox::Value configuration, std::string logfile)
    : atl::Heartbeat(DAEMON_HEARTBEAT_TIMEOUT),
    m_serverEKG(nullptr),
    m_daemonManager("AquetiDaemonServer")
{
    // init atomics
    m_doingOkay = true;

    initializeLogging(logfile);
    m_configuration = configuration;
    initialize();
}

void AquetiDaemonServer::initializeLogging(std::string logfile)
{
    //Load the hardware identified file
    ifstream hwID;
    hwID.open(ID_PATH);
    if(hwID.good()){
        std::getline(hwID,m_id);
    } else {
        std::cerr << "Hardware ID file /etc/aqueti/guid doesn't exist, using a default identifeir DEFAULT_DAEMON" << std::endl;
        m_id = "DEFAULT_DAEMON";
    }
    if (m_id.size() == 0) {
        std::cerr << "Hardware ID from file /etc/aqueti/guid is empty, using a default identifeir DEFAULT_DAEMON" << std::endl;
        m_id = "DEFAULT_DAEMON";
    }

    //Construct my logger object so I can immediately start using it
    std::shared_ptr<LumberjackServer> logger = std::make_shared<LumberjackServer>(this, JsonBox::Value(), logfile);
    logger->setLumberjack(logger);
    this->setLumberjack(logger);
    if(!m_activeSubmodules.emplace(logger->getSubmoduleDescriptor(),logger)){
        std::cerr << m_id << ": CRITICAL: Failed to emplace logging module, logging failure" << std::endl;
    }
    LOG_DEBUG(m_id,"Starting daemon with ID " + m_id);
}

void AquetiDaemonServer::initialize()
{
    //Start the run time clock
    m_runTimer.start();

    // launch pulse thread
    m_pulsar.setMainLoopFunction(
            [this](){
	            if(!m_restart){
                    pulse();
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            });
    m_pulsar.Start();

    // Figure out whether to make a LumberMill
    if(m_configuration["directoryOfServices"]["type"].tryGetString("")=="server"){
        //Also construct the lumbermill and emplace it into the map
        //std::shared_ptr<aqt::ServerSubmodule> submodulePtr(new LumberMillServer(this,{},m_logger));
        JsonBox::Value val;
        std::shared_ptr<aqt::ServerSubmodule> submodulePtr = std::make_shared<LumberMillServer>(this,val,m_logger);
        m_lms = submodulePtr;
        if(!m_activeSubmodules.emplace(submodulePtr->getSubmoduleDescriptor(),submodulePtr)){
            LOG_ERROR(m_id,"Failed to initialize log manager submodule");
        }
    }

    m_systemId = m_configuration["directoryOfServices"]["system"].tryGetString("");

    if(m_configuration["directoryOfServices"]["serverPort"].isInteger()){
        m_port = m_configuration["directoryOfServices"]["serverPort"].getInteger();
        LOG_DEBUG(m_id, "Port specified in daemon configuration, using port " + std::to_string(m_port) + " for daemon");
    }
    
    //Start my property manager server
    if(!initPropertyManagerServer()){
        LOG_CRITICAL(m_id, "Failed to start property manager server, exiting");
        exit(0);
    }

    //Spawn the things that my local config told me to spawn
    //LOG_DEBUG(m_id, "Initializing daemon resources");
    //if(!initDaemonResources()){
    //    LOG_ERROR(m_id,"Failed to intialize daemon resources");
    //}

    // get database info from config
    JsonBox::Value localDB(m_configuration["localDatabase"].getObject());
    m_localDB.uri = localDB["URI"].tryGetString(m_localDB.uri);
    m_localDB.name = localDB["name"].tryGetString(m_localDB.name);

    JsonBox::Value globalDB(m_configuration["globalDatabase"].getObject());
    m_globalDB.uri = globalDB["URI"].tryGetString(m_globalDB.uri);
    m_globalDB.name = globalDB["name"].tryGetString(m_globalDB.name);

    // make all other submodules
    LOG_DEBUG(m_id, "Initializing daemon submodules");
    if(!initSubmodulesFromConfig()){
        LOG_CRITICAL(m_id, "Failed to construct all submodules from config, exiting");
        exit(0); //TODO use a legit return code here
    }

    //Configurate and start my avahibroadcast
    LOG_DEBUG(m_id,"Starting service broadcast with port " +
            std::to_string(m_connectionStruct.m_port) +" and system ID " + m_systemId);

#ifndef _WIN32
    m_AvahiBroadcast = new atl::AvahiBroadcast();
    m_AvahiBroadcast->Start();

    m_broadcastParams["system"].setString(m_systemId);
    //TODO add "I am part of scop_X" to the daemon config file and put it in params when running on Tegras
    //bcastParams["SCOP"].setString(<scop_ID>)
	m_avahiBroadcastName = "AquetiDaemon_"+m_id+"_";
    m_AvahiBroadcast->createService(
			m_avahiBroadcastName + "_" + std::to_string(acl::getUsecTime())
            ,m_connectionStruct.m_port
            ,m_broadcastParams
            );

//	m_AvahiBroadcast->reset();
//
//    m_AvahiBroadcast->createService(
//            "AquetiDaemon_"+m_id
//            ,m_connectionStruct.m_port
//            ,m_broadcastParams
//            );
//
#endif

    //Start this thread
    Start();

    // Figure out which type of directory of services to make 
    std::unique_lock<std::mutex> l(m_configMutex);
    if(m_configuration["directoryOfServices"]["type"].getString()=="server"){
        LOG_DEBUG(m_id, "Constructing a DOS server");
        m_directoryOfServices = new DirectoryServicesServer(
                this
                ,[this](aqt::SubmoduleDescriptor desc,atl::PropertyManagerCreated conn){handleCreatedSubmodule(desc,conn);}
                ,[this](aqt::SubmoduleDescriptor desc){handleDestroyedSubmodule(desc);}
                ,m_configuration["directoryOfServices"]
                ,m_logger);
    }else if(m_configuration["directoryOfServices"]["type"].getString()=="client"){
        LOG_DEBUG(m_id, "Constructing a DOS client, waiting for DOS server to come online");
    }else{
        LOG_CRITICAL(m_id,"Directory of services missing from config, specify directoryOfServices client/server and restart (exiting)"); 
        exit(0);
    }
    l.unlock();
    
    if(m_directoryOfServices){
    std::cout << 
        "=====================================================\n"
        <<"          AquetiDaemonServer fully initialized     \n"<<
        "=====================================================\n";
    } else {
        std::cout <<
        "==========================================================\n"
    <<  "  AquetiDaemonServer intialized and waiting on DOS server \n" <<
        "==========================================================\n";
    }
}


AquetiDaemonServer::~AquetiDaemonServer()
{
    m_destructorFlag = true;
    std::lock_guard<std::mutex> l(m_destructorMutex);

    Stop();
    Join();
    if(m_updateReceiver){
        delete m_updateReceiver;
    }

    LOG_DEBUG(m_id,"Destructing clearing submodule map");
    for(aqt::SubmoduleDescriptor mapkey : m_activeSubmodules.getKeyList()){
        m_activeSubmodules.erase(mapkey);
        m_directoryOfServices->submoduleDestroyed(mapkey);
    }

    LOG_DEBUG(m_id,"Destructing DOS object");
    if(m_directoryOfServices){
        delete m_directoryOfServices;
    }

#ifndef _WIN32
    LOG_DEBUG(m_id,"Destructing Avahi broadcast");
    if(m_AvahiBroadcast){
        delete m_AvahiBroadcast;
    }
#else
    LOG_DEBUG(m_id, "Avahi broadcast ignored on Windows, so not destroyed");
#endif

    //some janky shit so lumberjack doesn't segfault because it has a shared ptr
    //to itself
    dynamic_pointer_cast<LumberjackServer>(m_logger)->setLumberjack(nullptr);
    if (m_lms) m_lms->setLumberjack(nullptr);

    // stop/join pulse thread
    m_pulsar.Stop();
    m_pulsar.Join();
}


/**
 *  Thread Main loop
 **/
void AquetiDaemonServer::mainLoop()
{

    //Check the references to my client submodules, if I have the only
    //reference get rid of it
    for(aqt::SubmoduleDescriptor mapKey : m_clientSubmodules.getKeyList()){
        auto clientMod = m_clientSubmodules.find(mapKey);
        if(clientMod.second){
            if(clientMod.first == nullptr || !clientMod.first->isConnected()) {
                LOG_WARNING(m_id, "Found client submodule with failed connection, deleting and notifying submodules of deletion: " + mapKey.print());
                handleDestroyedSubmodule(mapKey);
            } else {
                long ptrReferences = (clientMod.first).use_count();
                if(ptrReferences == 1){
                    m_clientSubmodules.remove(mapKey);
                    LOG_DEBUG(m_id, "Removing a client submodule, reference count is 1");
                }
            }
        }
    }

    //debug json print
    for(int i =0; i < 10; i++){
        getUpdatedClockInfo();
    }
    

	if(!(m_loopCount++ % 50)){
#ifndef _WIN32
//		m_AvahiBroadcast->reset();
//		m_AvahiBroadcast->Stop();
		delete m_AvahiBroadcast;
		m_AvahiBroadcast = nullptr;


		m_AvahiBroadcast = new atl::AvahiBroadcast();
		m_AvahiBroadcast->Start();

        // lock config mutex to prevent race condition with updating config
        std::unique_lock<std::mutex> l(m_configMutex);
		m_broadcastParams["system"].setString(m_systemId);
        l.unlock();

		m_AvahiBroadcast->createService(
			//	m_avahiBroadcastName
				m_avahiBroadcastName+"_"+std::to_string(acl::getUsecTime())
//				"AquetiDaemon_"+m_id //+"_"+std::to_string(atl::getUsecTime())
		//	    "AquetiDaemon_"+m_id
			    ,m_connectionStruct.m_port
			    ,m_broadcastParams
			    );
#endif
    } 

    //check my DOS pointer
    if(m_directoryOfServices){
        DirectoryServicesClient* myDosClient = dynamic_cast<DirectoryServicesClient*>(m_directoryOfServices);
        if (myDosClient) {
            LOG_DEBUG(m_id,"Checking my DOS connection");
            if (!myDosClient->isConnected()) {
                LOG_WARNING(m_id, "DOS client disconnected, clearing DOS pointer");
                delete m_directoryOfServices;
                m_directoryOfServices = nullptr;
            }
        }
    }

    //std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

/**
 *  Public member functions
 */

std::shared_ptr<aqt::BaseSubmodule> AquetiDaemonServer::getSubmodule(aqt::SubmoduleDescriptor submoduleID)
{
    std::unique_lock<std::mutex> l(m_destructorMutex, std::defer_lock);
    if (!l.try_lock()) {
        if (m_destructorFlag) {
            LOG_DEBUG(m_id, "Cannot request submodules during daemon destruction");
            return nullptr;
        } else {
            l.lock();
        }
    }

    //Check my local modules
    auto mapEntry = m_activeSubmodules.find(submoduleID);
    if(!mapEntry.second){
        LOG_WARNING(m_id, "Failed to find " + submoduleID.ID + " in local map checking client map");
    }else{
        LOG_DEBUG(m_id, "Satisfied request for submodule " + submoduleID.print());
        return mapEntry.first;
    }
    //Check clients that if already made
    auto mapEntryclient = m_clientSubmodules.find(submoduleID);
    if(!mapEntryclient.second){
        LOG_DEBUG(m_id, "Failed to find " + submoduleID.ID + " in client map, asking Directory of services");
    } else if (!mapEntryclient.first->isConnected()) {
        LOG_WARNING(m_id, "Found disconnected client submodule " + submoduleID.ID + ", asking Directory of services for a new one");
    } else {
        LOG_DEBUG(m_id, "Satisfied request for submodule " + submoduleID.ID);
        return mapEntryclient.first;
    }
    //I dont have it get from directory of services
    if(m_directoryOfServices!=nullptr){
        atl::PropertyManagerCreated connStruct = m_directoryOfServices->getSubmoduleConnStruct(submoduleID);
        auto requestedModule = createClientFromRemote(submoduleID,connStruct);
        if (requestedModule == nullptr) {
            LOG_ERROR(m_id, "Failed to create client submodule for: " + submoduleID.print());
            return nullptr;
        } else if(!m_clientSubmodules.emplace(submoduleID,requestedModule,true)){ //true replaces existing disconnected entry
            LOG_ERROR(m_id,"Failed to emplace client submodule in map");
        }
        return std::dynamic_pointer_cast<aqt::BaseSubmodule>(requestedModule);
    } else {
        LOG_ERROR(m_id,"Got a request for a submodule that doesn't exist, someone is doing something wrong");
    }
    return nullptr;
}

bool AquetiDaemonServer::addSubmodule(std::shared_ptr<aqt::BaseSubmodule> submodule)
{
    bool rc = true;
    std::shared_ptr<aqt::ServerSubmodule> internalPtr;
    internalPtr = std::dynamic_pointer_cast<aqt::ServerSubmodule>(submodule);
    LOG_DEBUG(m_id, "Local submodule adding another submodule to daemons map");
    if(!m_activeSubmodules.emplace(submodule->getSubmoduleDescriptor(),internalPtr)){
        LOG_ERROR(m_id, "Failed to emplace submodule in daemon map might already exist");
        rc = rc && false;
    } else {
        LOG_DEBUG(m_id, "Emplaced submodule in map informing directory of services");
    }
    if(m_directoryOfServices != nullptr && internalPtr->getConnectionStruct()){
        m_directoryOfServices->submoduleCreated(
                submodule->getSubmoduleDescriptor()
                ,internalPtr->getConnectionStruct());
    } else {
        if(m_directoryOfServices == nullptr){
            LOG_DEBUG(m_id, "Directory of services is still a nullptr, will call submoduleCreated later");
        } else {
            LOG_ERROR(m_id, "Local submodule attmepted to add and invalid submodule to map");
        }
    }

    return rc;
}

std::shared_ptr<aqt::BaseDaemonResource> AquetiDaemonServer::getDaemonResource(std::string ID)
{
    // lock a mutex to avoid the race where two things ask for 
    // the same uncreated resource simultaneously
    std::lock_guard<std::recursive_mutex> l(m_resourceMutex);

    // check the map of daemon resources
    auto mapEntry = m_daemonResources.find(ID);
    if(!mapEntry.second){
        // if we cant find it, try to make it
        if (createNewDaemonResource(ID)) {
            mapEntry = m_daemonResources.find(ID);
        }
    }

    if (!mapEntry.second || !mapEntry.first) {
        LOG_ERROR(m_id, "Failed to satify request for daemon resource " + ID);
        return nullptr;
    } else {
        LOG_DEBUG(m_id,"Satisfied return for daemon resource " + ID);
        return mapEntry.first;
    }
}

bool AquetiDaemonServer::setNewSubmoduleCallback(
        std::function<void(aqt::SubmoduleDescriptor)> callback, 
        std::string yourid, 
        std::string type 
        )
{
    aqt::SubmoduleDescriptor desc;
    desc.ID = yourid;
    desc.type = type;
    LOG_DEBUG(m_id, "Submodule "+yourid+" bound callback to "+type+" submodules");
    if(!m_callbackFunctions.emplace(desc,callback))
    {
        LOG_ERROR(m_id, "Failed to emplace submodule "+yourid+" new submodule callback function");
        return false;
    }else{
        LOG_DEBUG(m_id, "Emplaced submodule "+yourid+" new submodule callback function");
        return true;
    }
}


bool AquetiDaemonServer::setDeletedSubmoduleCallback(
        std::function<void(aqt::SubmoduleDescriptor)> callback, 
        std::string yourid, 
        std::string type 
        )
{
    aqt::SubmoduleDescriptor desc;
    desc.ID = yourid;
    desc.type = type;
    LOG_DEBUG(m_id, "Submodule "+yourid+" bound deleted callback to "+type+" submodules");
    if(!m_deletedFunctions.emplace(desc,callback))
    {
        LOG_ERROR(m_id, "Failed to emplace submodule "+yourid+" new submodule callback function");
        return false;
    }else{
        LOG_DEBUG(m_id, "Emplaced submodule "+yourid+" deleted submodule callback function");
        return true;
    }
}

std::vector<aqt::SubmoduleDescriptor> AquetiDaemonServer::getSubmoduleList(std::string submoduleType)
{
    std::vector<aqt::SubmoduleDescriptor> submoduleList;
    if(m_directoryOfServices!=nullptr){
        submoduleList = m_directoryOfServices->getSubmoduleList(submoduleType);
    }
    if (submoduleList.empty()) {
        for(aqt::SubmoduleDescriptor smod : m_activeSubmodules.getKeyList()){
            if(smod.type==submoduleType || submoduleType.empty()){
                submoduleList.push_back(smod);
            }
        }
    }
    return submoduleList;
}

std::vector<std::string> AquetiDaemonServer::getDaemonResourceList()
{
    return m_daemonResources.getKeyList();
}

JsonBox::Value AquetiDaemonServer::getDetailedStatus(const std::string &entity,
        aqt::aqt_Status *status)
{
    LOG_DEBUG(m_id,"Detailed status requested for "+entity);
    JsonBox::Value val;
    val["software"].setString(aqt::AqtVersion::getAqtVersionString());
    val["id"].setString(m_id);
    val["type"].setString("AquetiDaemon");
    val["ntp"] = getNTPSettings();
    val["parameters"] = getParameters("");
    JsonBox::Array submods;
    for(aqt::SubmoduleDescriptor key : m_activeSubmodules.getKeyList()){
        JsonBox::Value submod;
        submod[key.type].setString(key.ID);
        submods.push_back(submod);
    }
    val["submodules"].setArray(submods);
    val["RAM"] = getRamStatusJson();
    //val["CPU"] = getCpuStatusJson();
    //val["GPU"] = getGpuStatusJson();
    //val["drives"] = getDriveStatusJson();
    val["runtime"].setDouble(m_runTimer.elapsed());
    val["state"] = getSystemStateJson(val);
    return val;
}

JsonBox::Value AquetiDaemonServer::getParameters(
        const std::string &entity
        , aqt::aqt_Status *status)
{ 
    LOG_DEBUG(m_id,"Parameters requested for "+entity);
    JsonBox::Value val;
    //TODO get this from an actual method
    val["networks"].setArray(getNetworkJson());
    val["restartDaemon"].setBoolean(false);
    val["restartSystem"].setBoolean(false);
    val["shutdownSystem"].setBoolean(false);
    val["globalDatabase"]["URI"].setString(m_globalDB.uri);
    val["globalDatabase"]["name"].setString(m_globalDB.name);
    val["localDatabase"]["URI"].setString(m_localDB.uri);
    val["localDatabase"]["name"].setString(m_localDB.name);

    // lock config mutex to prevent race condition with update config
    std::unique_lock<std::mutex> l(m_configMutex);
    if(!m_configuration["directoryOfServices"]["system"].isNull()){
        val["system"].setString(m_configuration
                ["directoryOfServices"]
                ["system"].getString());
    }
    val["configuration"] = m_configuration;
    l.unlock();

    return val;
}

JsonBox::Value AquetiDaemonServer::setParameters(
        const std::string &entity
        ,const JsonBox::Value &params
        ,aqt::aqt_Status *status)
{
    JsonBox::Value newParams = params;
    bool restart = false;
    bool restartSys = false;
    bool shutdownSys = false;
    //start checking if the fields are true
    if(!newParams["networks"].isNull()){
        //change that damn IP and restart
        for(JsonBox::Value network : newParams["networks"].getArray()){
            if(!network["ip"].isNull() && !network["netmask"].isNull()){
                updateInterfaceIpNetmask(
                        network["interface"].getString()
                        ,network["ip"].getString()
                        ,network["netmask"].getString());
            }
        }
        restart = true;
    }

    if(!newParams["system"].isNull()){
        //change that damn system and update broadcast
    }

    if(!newParams["configuration"].isNull()){
        //change that damn config and restart
    }

    if(newParams["shutdownSystem"].getBoolean()){
        shutdownSys = true;
    }

    if(newParams["restartDaemon"].getBoolean()){
        //exit()
        restart = true;
    }

    if(newParams["restartSystem"].getBoolean()){
        restartSys = true;
    }

    if(!newParams["ntpServers"].isNull()){
        std::vector<std::string> servs;
        for(JsonBox::Value serv : newParams["ntpServers"].getArray()){
            servs.push_back(serv.getString());
        }
        addNtpServer(servs);
    }

    if(newParams["syncNtp"].getBoolean()){
        LOG_DEBUG(m_id,"Force ntp time sync triggered, syncing to my servers");
        forceNtpSync();
    }

    if(restart){
        LOG_DEBUG(m_id, "Received signal to restart system, stopping heartbeat");
        m_restart=true;
    }
    
    if(shutdownSys){
        shutdownSystem();
    }

    if(restartSys){
        restartSystem();
    }

    return getParameters("");
}

int AquetiDaemonServer::prepareDaemonForSoftwareUpdate(int numberOfPackages)
{
    //Create a SocketContainerReceiver
    m_updateReceiver = new atl::SocketContainerReceiver(0);
    m_updateReceiver->setPushFunction([this,numberOfPackages](apl::BaseContainer basecon)->bool{ 
            m_packageContainers.set_max_size(numberOfPackages);
            return m_packageContainers.enqueue(basecon);});
    return m_updateReceiver->getPort();
}

int AquetiDaemonServer::updateDaemonSoftwareandRestart(std::string packageHash)
{
    //Clean up submodules and pointers
    for(aqt::SubmoduleDescriptor mapkey : m_activeSubmodules.getKeyList()){
        m_activeSubmodules.erase(mapkey);
        m_directoryOfServices->submoduleDestroyed(mapkey);
    }

    LOG_DEBUG(m_id,"Destructing DOS object");
    if(m_directoryOfServices){
        delete m_directoryOfServices;
        m_directoryOfServices = nullptr;
    }

#ifndef _WIN32
    LOG_DEBUG(m_id,"Destructing Avahi broadcast");
    if(m_AvahiBroadcast){
        delete m_AvahiBroadcast;
        m_AvahiBroadcast = nullptr;
    }
#else
    LOG_DEBUG(m_id, "Avahi broadcast not available on Windows, not destructing");
#endif

    //pull the containers off the Queue
    apl::BaseContainer bc;
    m_packageContainers.dequeue(bc);
    //TODO do some cool thing here to make sure the hash of the package is correct
    std::cout << "you sent me a package hash of " << packageHash << std::endl;
    //write the bytes to a file 
    //TODO write the files to var/tmp/aqueti/packages or something similar
    FILE* diskData = fopen("AquetiUpdate.deb","w");
    fwrite(bc.getReadPointer(),1,bc.getDataSize(),diskData);
    fclose(diskData);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    //invoke a system call to install in
    int rc = std::system("dpkg -i AquetiUpdate.deb");
    if (rc == 0) {
        //set m_restart to true
        m_restart=true;
    }
    //TODO use some intelligent return codes if there are failure modes
    return rc;
}

/**
 * Private member functions
 */
//bool AquetiDaemonServer::initDaemonResources()
//{
//    bool rc = true;
//    JsonBox::Array resourceArray = m_configuration["resource"].getArray();
//    for(JsonBox::Value res : resourceArray){
//        LOG_DEBUG(m_id,"creating daemon resource of type " + res["type"].getString());
//        std::shared_ptr<aqt::BaseDaemonResource> resPtr = nullptr;
//        if (res["type"]=="ContainerCache") {
//            resPtr = std::make_shared<atl::ContainerCache>(m_logger);
//        } else if (res["type"]=="DataRequestRouter") {
//            resPtr = std::make_shared<DataRequestRouter>(this,m_logger);
//        } else {
//            LOG_ERROR(m_id,"Unrecognized daemon resource type in /etc/aqueti/daemonConfiguration.json");
//            rc = false && rc;
//        }
//        if (resPtr != nullptr && !m_daemonResources.emplace(res["type"].getString(),resPtr)) {
//            LOG_ERROR(m_id,"Failed to emplace resource in map, probably asking me to make two of the same thing");
//            rc = false && rc;
//        }
//    }
//    return rc;
//}

bool AquetiDaemonServer::createNewDaemonResource(std::string ID)
{
    LOG_DEBUG(m_id,"creating daemon resource of type " + ID);

    // attempt to make the requested resource
    std::shared_ptr<aqt::BaseDaemonResource> resPtr = nullptr;
    if (ID == "ContainerCache") {
        resPtr = std::make_shared<atl::ContainerCache>(m_logger);
    } else if (ID == "DataRequestRouter") {
        resPtr = std::make_shared<DataRequestRouter>(this,m_logger);
    } else {
        LOG_ERROR(m_id,"Unrecognized daemon resource type " + ID);
        return false;
    }

    // try to add the new resource to the map
    if (resPtr != nullptr && !m_daemonResources.emplace(ID, resPtr)) {
        LOG_ERROR(m_id,"Failed to emplace resource " + ID
                + " in map, probably asking me to make two of the same thing");
        return false;
    }
    return true;
}

bool AquetiDaemonServer::initSubmodulesFromConfig()
{
    bool rc = true;
    JsonBox::Array smArray = m_configuration["submodule"].getArray();
    for(JsonBox::Value submodule : smArray){

        if(submodule["type"].getString()=="capture"){
            LOG_DEBUG(m_id,"Launching a capture submodule");
            if(!createNewCaptureModule(submodule["compressionMode"].getString(),
                        submodule["scales"].getInteger(),submodule["baseScale"].getString())){
                LOG_ERROR(m_id,"Failed to spawn local capture instance");
                rc = rc && false;
            }
        }else{
            rc = rc && createNewSubmodule(submodule);
        }
    }
    return rc;
}

bool AquetiDaemonServer::createNewSubmodule(JsonBox::Value submodule)
{
    bool rc = true;
    LOG_DEBUG(m_id,"createNewSubmodule of type "+submodule["type"].getToString());
    std::shared_ptr<aqt::ServerSubmodule> submodulePtr;

	if(submodule["type"]=="MicroCameraManager"){
#ifdef TEGRA
		submodulePtr = 
			std::make_shared<aqt::MicroCameraManager>(this,submodule);
		submodulePtr->setLumberjack(m_logger);
	} else {
       return true;
    }
#else
    return true;	
} else if(submodule["type"]=="Hyperion"){
    submodulePtr = std::make_shared<HyperionServer>(this,submodule,m_logger);
} else if(submodule["type"]=="Cronus"){
    submodulePtr = std::make_shared<CronusServer>(this,submodule,m_logger);
} else if(submodule["type"]=="Coeus"){
    submodulePtr = std::make_shared<CoeusServer>(this,submodule,m_logger);
} else if(submodule["type"]=="Mnemosyne"){
    submodulePtr = std::make_shared<MnemosyneServer>(this,submodule,m_logger);
} else if(submodule["type"]=="SCOPController"){
    submodulePtr = std::make_shared<aqt::SCOPController>(this,submodule,m_logger);
} else if(submodule["type"]=="ModelHandler"){
    submodulePtr = std::make_shared<aqt::ModelHandlerServer>(this,submodule,m_logger);
} else if(submodule["type"]=="ImportExportControl"){
    submodulePtr = std::make_shared<ImportExportControlServer>(this,submodule,m_logger);
} else{
    LOG_WARNING(m_id,"Unrecognized submodule type");
}
#endif

if (!submodulePtr) {
    LOG_WARNING(m_id, "Failed to make unrecognized submodule of type " + submodule["type"].getString());
    return true; // becauase this should be handled gracefully, unlike failing to make a known type
}

// emplace the submodule in the map
aqt::SubmoduleDescriptor desc;
desc = submodulePtr->getSubmoduleDescriptor();
if (!desc) {
    LOG_ERROR(m_id,"Failed to get description of submodule");
    rc = rc && false;
}
else {
  LOG_DEBUG(m_id, "added a "+desc.type+" submodule to map");
  if(!m_activeSubmodules.emplace(desc, submodulePtr)){
    LOG_ERROR(m_id,"Failed to emplace submodule in map");
    rc = rc && false;
  }
}

////Alert my directory of services that I made a new submodule
//if(m_directoryOfServices!=nullptr){
//    if(!m_directoryOfServices->
//            submoduleCreated(desc,submodulePtr->getConnectionStruct())){
//        LOG_ERROR(m_id,"Failed to alert DOS of new submodule: " + desc.print());
//        rc = rc && false;
//    }
//} else {
//    LOG_WARNING(m_id,"Local DOS is null, waiting to alert DOS of submodules");
//}

//Alert any local things that care about this type
alertCallbackFunctions(desc);
return rc;
}

std::shared_ptr<aqt::ClientSubmodule> AquetiDaemonServer::createClientFromRemote(
        aqt::SubmoduleDescriptor submod, 
        atl::PropertyManagerCreated connStruct
        )
{
    std::shared_ptr<aqt::ClientSubmodule> returnModule = nullptr;
#ifndef TEGRA
    if(submod.type == "MicroCameraManager"){
        LOG_DEBUG(m_id,"Making a MicroCameraManager client for: " + submod.print());
		returnModule = std::make_shared<aqt::MicroCameraManagerClient>(connStruct, m_logger);
	} else if(submod.type == "Hyperion"){
        LOG_DEBUG(m_id,"Making a Hyperion client for: " + submod.print());
        returnModule = std::make_shared<HyperionClient>(connStruct,m_logger);
    } else if (submod.type == "Cronus"){
        LOG_DEBUG(m_id,"Making a Cronus client for: " + submod.print());
        returnModule = std::make_shared<CronusClient>(connStruct,m_logger);
    } else if (submod.type == "Mnemosyne"){
        LOG_DEBUG(m_id,"Making a Mnemosyne client for: " + submod.print());
        returnModule = std::make_shared<MnemosyneClient>(connStruct,m_logger);
    } else if (submod.type == "Coeus") {
        LOG_DEBUG(m_id,"Making a Coeus client for: " + submod.print());
        returnModule = std::make_shared<CoeusClient>(connStruct,m_logger);
    } else if(submod.type == "SCOPController"){
        LOG_DEBUG(m_id,"Making a SCOPController client for: " + submod.print());
        returnModule = std::make_shared<aqt::SCOPControllerClient>(connStruct,m_logger);
    } else if(submod.type == "Lumberjack") {
        LOG_DEBUG(m_id,"Making a Lumberjack client for: " + submod.print());
        returnModule = std::make_shared<LumberjackClient>(connStruct,m_logger);
    } else {
        LOG_ERROR(m_id,"Asked to make a client submodule of unrecognized type "
                + submod.type + ", this daemon may be out of date");
    }
#endif

    return returnModule;
}

bool AquetiDaemonServer::createNewCaptureModule(std::string compressionMode, int scales, std::string baseScale)
{
    std::string launchStr;
    launchStr.append("acosd -C ");
    launchStr.append(compressionMode);
    launchStr.append(" -s ");
    launchStr.append(std::to_string(scales));
    launchStr.append(" -r ");
    launchStr.append(baseScale);
    launchStr.append(" -vvv -R acosd");
    int rc = std::system(launchStr.c_str());
    std::cout << "base scale info: " << baseScale << std::endl;
    std::cout << ("Started an acosd subprocess") << std::endl;
    std::cout << "Acosd subprocess info: " << launchStr << std::endl;
    if (rc == 0) {
        return true;
    }
    return false;
}

bool AquetiDaemonServer::initPropertyManagerServer()
{
    bool rc = true;
    atl::PropertyManagerCreated connStruct = m_daemonManager.startServer(m_port);

    if(connStruct){
        LOG_DEBUG(m_id, "Started property manager server on port "+std::to_string(connStruct.m_port));
        m_connectionStruct = connStruct;
    }else{
        rc = rc && false;
    }
    // Register Directives
    atl::Directive<std::string,atl::StateVector<aqt::SubmoduleDescriptor>> getSubmoduleListDirective(
            [this](std::string moduleType)->atl::StateVector<aqt::SubmoduleDescriptor>{
            return getSubmoduleList(moduleType);
            });
    m_getSubmoduleList = std::move(getSubmoduleListDirective);

    atl::Directive<aqt::SubmoduleDescriptor,atl::PropertyManagerCreated> getSubmoduleConnStructDirective(
            [this](aqt::SubmoduleDescriptor submodID)->atl::PropertyManagerCreated {
            return getSubmoduleConnStruct(submodID);
            });
    m_getSubModuleConnectionStruct = std::move(getSubmoduleConnStructDirective);

    atl::Directive<atl::PropertyManagerCreated,bool> startDOSDirective(
            [this](atl::PropertyManagerCreated DOSconn)->bool {
            return initDirectoryOfServicesClient(DOSconn);
            });
    m_initDirectoryServices = std::move(startDOSDirective);

    atl::Directive<std::string,std::string> getDaemonStatusDirective(
            [this](std::string entity)->std::string {
            JsonBox::Value val = getDetailedStatus(entity);
            std::string ret = val.getToString();
            return ret;
            });
    m_getDetailedStatus = std::move(getDaemonStatusDirective);

    atl::Directive<std::string,std::string> getDaemonParametersDirective(
            [this](std::string entity)->std::string {
            JsonBox::Value val = getParameters(entity);
            std::string ret = val.getToString();
            return ret;
            });
    m_getParameters = std::move(getDaemonParametersDirective);

    atl::Directive<std::string,std::string> setDaemonParametersDirective(
            [this](std::string params)->std::string {
            JsonBox::Value jsonparams = atl::tryLoadJsonFromString(params);
            JsonBox::Value val = setParameters("",jsonparams);
            std::string ret = val.getToString();
            return ret;
            });
    m_setParameters = std::move(setDaemonParametersDirective);

    atl::Directive<int,int> prepareSoftwareUpdateDirective(
            [this](int packageNumber)->int {
            return prepareDaemonForSoftwareUpdate(packageNumber);
            });
    m_prepareSoftwareUpdate = std::move(prepareSoftwareUpdateDirective);

    atl::Directive<std::string,int> softwareUpdateDirective(
            [this](std::string packageHash)->int {
            return updateDaemonSoftwareandRestart(packageHash);
            });
    m_softwareUpdate = std::move(softwareUpdateDirective);

    if(!m_daemonManager.setDirective(GET_SUBMODULE_LIST,m_getSubmoduleList)){
        LOG_ERROR(m_id, "Failed to set directive for submodule list");
        rc = rc && false;
    }

    if(!m_daemonManager.setDirective(GET_SUBMODULE_CONNECTION_STRUCT,m_getSubModuleConnectionStruct)){
        LOG_ERROR(m_id, "Failed to set directive for get submodule connection struct");
        rc = rc && false;
    }

    if(!m_daemonManager.setDirective(INIT_DOS_CLIENT, m_initDirectoryServices)){
        LOG_ERROR(m_id, "Failed to set directive for init directory of services");
        rc = rc && false;
    }
    
    if(!m_daemonManager.setDirective(GET_DAEMON_STATUS, m_getDetailedStatus)){
        LOG_ERROR(m_id, "Failed to set directive for getDetailedStatus");
        rc = rc && false;
    }

    if(!m_daemonManager.setDirective(GET_DAEMON_PARAMETERS, m_getParameters)){
        LOG_ERROR(m_id, "Failed to set directive for getParameters");
        rc = rc && false;
    }

    if(!m_daemonManager.setDirective(SET_DAEMON_PARAMETERS, m_setParameters)){
        LOG_ERROR(m_id, "Failed to set directive for setParameters");
        rc = rc && false;
    }

    if(!m_daemonManager.setDirective(PREPARE_FOR_UPDATE, m_prepareSoftwareUpdate)){
        LOG_ERROR(m_id, "Failed to set directive for prepareDaemonForSoftwareUpdate");
    }

    if(!m_daemonManager.setDirective(UPDATE_SOFTWARE, m_softwareUpdate)){
        LOG_ERROR(m_id, "Failed to set directive for prepareDaemonForSoftwareUpdate");
        rc = rc && false;
    }
    //Register Properties
    if(!m_daemonManager.setValue<std::string>(HOST_ID, m_id, true)){
        LOG_ERROR(m_id,"Failed to initialize the HOST_ID property for AquetiDaemonServer");
        rc = rc && false;
    }
    if(!m_daemonManager.setValue<std::string>(SYSTEM_ID, m_systemId, true)){
        LOG_ERROR(m_id,"Failed to initialize the SYSTEM_ID property for AquetiDaemonServer");
        rc = rc && false;
    }

    return rc;
}

void AquetiDaemonServer::handleCreatedSubmodule(
        aqt::SubmoduleDescriptor submodule
        , atl::PropertyManagerCreated connStruct)
{
    LOG_DEBUG(m_id,"Alerted by DOS of a new submodule "+submodule.ID);
    //Check if any of my local submodules care about this type, if so construct a std::shared_ptr of type
    //clientsubmodule from it, and emplace it in the client submodules map then call the callback with the descriptor
    //from above

    // if i give a fuck
    std::vector<aqt::SubmoduleDescriptor> submodules = m_callbackFunctions.getKeyList();
    std::function<void(aqt::SubmoduleDescriptor)> callbackFunc;
    for(aqt::SubmoduleDescriptor mapsubmod : submodules){                               
        if(mapsubmod.type == submodule.type){

            // if I made this, dont make a client, else do
            if (m_activeSubmodules.find(submodule).second) {
                LOG_DEBUG(m_id, "I OWN TINGS");
                LOG_WARNING(m_id, "Alerted of my own submodule, not making a client"); 
            } else {
                LOG_DEBUG(m_id, "One of my local submodules has a callback bound to "+mapsubmod.type+" making client");
                std::shared_ptr<aqt::ClientSubmodule> newClientSubmod = createClientFromRemote(submodule, connStruct);
                if (newClientSubmod == nullptr) {
                    LOG_ERROR(m_id, "Failed to create client submodule for: " + submodule.print());
                    return;
                } else if(!m_clientSubmodules.emplace(submodule,newClientSubmod)){
                    LOG_ERROR(m_id, "Failed to emplace client submodule in map");
                }
            }

            auto mapEntry = m_callbackFunctions.find(mapsubmod);
            if(mapEntry.second){
                LOG_DEBUG(m_id, "Alerting "+mapsubmod.ID+" of new submodule");
                callbackFunc = mapEntry.first;
                callbackFunc(submodule);
            } else { 
                LOG_ERROR(m_id,"Failed to retreive callback function from map");
            }
        }
    }
}


void AquetiDaemonServer::handleDestroyedSubmodule(aqt::SubmoduleDescriptor submodule)
{
    if(!m_clientSubmodules.remove(submodule).second){
        LOG_DEBUG(m_id,"Failed to erase client submodule from map, this usually means this daemon owned the server");
    } else {
        LOG_DEBUG(m_id,"Successfully removed client submodule from map");
        alertDeletedCallbackFunctions(submodule);
    }
    //TODO definintely need to have the submodules bind a callback to the daemon to be
    //alerted of submodule deletion so they don't try to use it
}

bool AquetiDaemonServer::initDirectoryOfServicesClient(atl::PropertyManagerCreated connStruct)
{
    LOG_DEBUG(m_id,"Directive called to initialize DOS client");
    bool rc = true;

    // try to make a client pointer
    DirectoryServicesClient *dsc = dynamic_cast<DirectoryServicesClient*>(m_directoryOfServices);
    // is client valid and connected
    bool con = dsc ? dsc->isConnected() : false;
    //TODO this gets overwritten if it gets recalled even if we dont necessarily want it to
    //fix
    if(!m_daemonManager.setValue<atl::PropertyManagerCreated>(DOS_CONNECTION,connStruct,true)){
        LOG_ERROR(m_id,"Failed to update directory of services connection property");
    } else {
        LOG_DEBUG(m_id,"DOS property set");
    }

    if(m_directoryOfServices==nullptr || (dsc && !con)){
        // if valid and not connected
        if(dsc && !con){	
            delete m_directoryOfServices;
        }

        LOG_DEBUG(m_id,"DOSserver online, connecting");
        DirectoryServicesClient* dosClient;
        dosClient = new DirectoryServicesClient(
                connStruct
                ,[this](aqt::SubmoduleDescriptor desc,atl::PropertyManagerCreated conn){handleCreatedSubmodule(desc,conn);}
                ,[this](aqt::SubmoduleDescriptor desc){handleDestroyedSubmodule(desc);});
        //Validate that we successfully connected to the server
        if(dosClient->isConnected()){
            LOG_DEBUG(m_id,"Successfully connected to the DOSserver, alerting of local submodules");
            m_directoryOfServices = dynamic_cast<DirectoryServices*>(dosClient);
            //Alert the directory of services all the local submodules we've created
            // TODO this does not appear to be working probably an issue with the directive
            for(aqt::SubmoduleDescriptor submod : m_activeSubmodules.getKeyList()){
                auto submodFound = m_activeSubmodules.find(submod); 
                if(submodFound.second){     
                    m_directoryOfServices->submoduleCreated(submod,(submodFound.first)->getConnectionStruct());
                } else {
                    LOG_ERROR(m_id,"Failed to get a submodule in my own map, something bad happened");
                    rc = rc && false;
                }
            }
            //Find out what the DOS knows about so far, necessary in the event that other daemons came online
            //before this.
            LOG_DEBUG(m_id,"Asking directory of services for it's known submodules");
            for(aqt::SubmoduleDescriptor submod : m_directoryOfServices->getSubmoduleList()){
                alertCallbackFunctions(submod);
            }

        } else {
            LOG_ERROR(m_id,"Directory of Service Client Failed to connect to Server");
            rc = rc && false;
        }
    } else {
        LOG_DEBUG(m_id,"Directory of services is already instantiated, I must own the DOS server");
        for(aqt::SubmoduleDescriptor submod : m_activeSubmodules.getKeyList()){
            auto submodFound = m_activeSubmodules.find(submod); 
            if(submodFound.second){     
                m_directoryOfServices->submoduleCreated(submod,(submodFound.first)->getConnectionStruct());
            } else {
                LOG_ERROR(m_id,"Failed to get a submodule in my own map, something bad happened");
                rc = rc && false;
            }
        }
    }

    return rc;
}


void AquetiDaemonServer::alertCallbackFunctions(aqt::SubmoduleDescriptor newSubmodule)
{
    std::vector<aqt::SubmoduleDescriptor> keyList = m_callbackFunctions.getKeyList();
    for(aqt::SubmoduleDescriptor key : keyList)
    {
        if(key.type == newSubmodule.type){
            auto callback = m_callbackFunctions.find(key);
            if(callback.second){
                callback.first(newSubmodule);
            }		

        }
    }
}

void AquetiDaemonServer::alertDeletedCallbackFunctions(aqt::SubmoduleDescriptor newSubmodule)
{
    std::vector<aqt::SubmoduleDescriptor> keyList = m_deletedFunctions.getKeyList();
    for(aqt::SubmoduleDescriptor key : keyList)
    {
        if(key.type == newSubmodule.type){
            auto callback = m_deletedFunctions.find(key);
            if(callback.second){
                callback.first(newSubmodule);
            }		

        }
    }
}

atl::PropertyManagerCreated AquetiDaemonServer::getSubmoduleConnStruct(aqt::SubmoduleDescriptor submoduleID)
{
    auto submod = m_activeSubmodules.find(submoduleID);
    if(submod.second){
        LOG_DEBUG(m_id,"Received request for submodule connection information");
        return (submod.first)->getConnectionStruct();
    }
    return {};
}

/**
 * @brief Hardware configuration functions
 **/
void AquetiDaemonServer::getUpdatedClockInfo()
{
#ifndef _WIN32
    memset(&m_timex,0,sizeof(timex));
    ntp_adjtime(&m_timex);
    if(m_NTPOffset.size() < NTP_OFFSET_SAMPLE_WINDOW){
        m_NTPOffset.push_back(static_cast<double>(m_timex.offset)/1e6);
    } else {
        m_NTPOffset.erase(m_NTPOffset.begin());
        m_NTPOffset.push_back(static_cast<double>(m_timex.offset)/1e6);
    }

    m_offset = getNTPoffset();
#else
    LOG_WARNING(m_id, "AquetiDaemonServer::getUpdatedClockInfo(): NTP not implemented on Windows");
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

bool AquetiDaemonServer::isNTPServer()
{

    std::ifstream ntpFile("/etc/ntp.conf");
    std::string line;
    std::string server;
    bool rc = false;
    while(std::getline(ntpFile,line)){
        std::string::size_type startpos;
        std::string::size_type pollpos;
        startpos = line.find("broadcast");
        pollpos = line.find("poll");
        if(startpos != std::string::npos && pollpos != std::string::npos){
            rc = true;
        }
    }
    return rc;
}

double AquetiDaemonServer::getNTPoffset()
{
    double meanvalue=0;
    for(double tmp : m_NTPOffset){
        meanvalue += tmp;
    }
    meanvalue/=m_NTPOffset.size();
    return meanvalue;
}

JsonBox::Value AquetiDaemonServer::getNTPSettings()
{
    JsonBox::Value val;
#ifndef _WIN32
    JsonBox::Array serverarray;
    val["isServer"].setBoolean(isNTPServer());
    val["time"].setInteger(m_timex.time.tv_sec);
    val["offset"].setDouble(m_offset);
    //get this from DOS
    //if(m_directoryOfServices){
    //    val["servers"].setArray(m_directoryOfServices->getDetailedStatus("/aqt/",nullptr)["ntpServers"].getArray());
    //} else {
    //    val["servers"].setArray(serverarray);
    //}
#else
    LOG_WARNING(m_id, "AquetiDaemonServer::getNTPSettings(): NTP not implemented on Windows");
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return val;
}

JsonBox::Array AquetiDaemonServer::getNetworkJson()
{
    JsonBox::Array networks;
    JsonBox::Value network;

#ifndef _WIN32
    struct ifaddrs* ifaddr, * ifa;
    int family, n;
    char host[NI_MAXHOST];

    if(getifaddrs(&ifaddr) == -1){
        LOG_ERROR(m_id,"Failed to get valid networking information from getiffaddrs");
        return networks;
    }

    for(ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++){
        if(ifa->ifa_addr==NULL){
            continue;
        }
    
        family = ifa->ifa_addr->sa_family;

        //TODO add || AF_INET6 if we start using ipv6
        if(family == AF_INET){
            network["interface"].setString(std::string(ifa->ifa_name));
            getnameinfo(ifa->ifa_addr
                    ,sizeof(struct sockaddr_in)
                    ,host
                    ,NI_MAXHOST
                    ,NULL
                    ,0
                    ,NI_NUMERICHOST);
            network["ip"].setString(
                    std::string(host));
            getnameinfo(ifa->ifa_netmask
                    ,sizeof(struct sockaddr_in)
                    ,host
                    ,NI_MAXHOST
                    ,NULL
                    ,0
                    ,NI_NUMERICHOST);
            network["netmask"].setString(
                    std::string(host));
            networks.push_back(network);
        }
    }

    freeifaddrs(ifaddr);
#else
    LOG_ERROR(m_id, "AquetiDaemonServer::getNetworkJson(): not implemented on Windows, use GetAdaptersAddresses");
#endif
    return networks;
}

void AquetiDaemonServer::updateInterfaceIpNetmask(
        std::string iface
        ,std::string ip
        ,std::string netmask)
{
    std::string cmd = "ip link set "+iface+" down";
    int rc = 0;
    try{        
        rc = std::system(cmd.c_str());
        if (rc != 0) {
            LOG_ERROR(m_id, "Failed to take down network interface");
        }
    } catch (int e) { 
    }

    std::string cmd2 = "ifconfig "+iface+" "+ip+" netmask"+" "+netmask;
    try{
        rc = std::system(cmd2.c_str());
        if (rc != 0) {
            LOG_ERROR(m_id, "Failed to set interface netmask");
        }
    } catch (int e) { 
    }

    std::string cmd3 = "ip link set "+iface+" up";
    try {
        rc = std::system(cmd3.c_str());
        if (rc != 0) {
            LOG_ERROR(m_id, "Failed to bring up network interface");
        }
    } catch (int e) { 
    }
}

JsonBox::Value AquetiDaemonServer::getCpuStatusJson()
{
    JsonBox::Value val; 
    val.setString("notImplemented");
    return val;
}


JsonBox::Value AquetiDaemonServer::getGpuStatusJson()
{
    JsonBox::Value val; 
    val.setString("notImplemented");
    return val;
}


JsonBox::Value AquetiDaemonServer::getRamStatusJson()
{
    JsonBox::Value val;

    FILE *meminfo = fopen("/proc/meminfo", "r");
    if (meminfo == NULL) {
        LOG_ERROR(m_id, "Failed to open /proc/meminfo");
    } else {
        // units are kB
        int totalram = 0;
        int availableram = 0;

        char line[256];
        while(fgets(line, sizeof(line), meminfo)) {
            sscanf(line, "MemTotal: %d kB", &totalram);
            sscanf(line, "MemAvailable: %d kB", &availableram);
        }
        fclose(meminfo);

        if (!totalram || !availableram) {
            LOG_ERROR(m_id, "Failed to parse RAM usage from /proc/meminfo");
        } else {
            double total = totalram;
            double used = totalram-availableram;
            val["total"].setDouble(total);
            val["used"].setDouble(used);
        }
    }
    return val;
}


JsonBox::Value AquetiDaemonServer::getDriveStatusJson()
{
    JsonBox::Value val;
    val.setString("notImplemented");
    return val;
}

JsonBox::Value AquetiDaemonServer::getSystemStateJson(JsonBox::Value& detailedStatus)
{
//#define AQT_GENERAL_HEALTH_OK "OK"
//#define AQT_GENERAL_HEALTH_WARNING "WARNING"
//#define AQT_GENERAL_HEALTH_ERROR "ERROR"
//#define AQT_GENERAL_HEALTH_CRITICAL "CRITICAL"
    JsonBox::Value val;
    val["generalHealth"].setString(AQT_GENERAL_HEALTH_OK);
    val["ntp"].setString(AQT_GENERAL_HEALTH_OK);
    val["directoryOfServices"].setString(AQT_GENERAL_HEALTH_OK);
    if(detailedStatus["ntp"]["offset"].getDouble() == 0){
        LOG_ERROR(m_id, "NTP offset is zero, this generally means we have no valid ntp servers");
        val["ntp"].setString(AQT_GENERAL_HEALTH_CRITICAL);
        val["general"].setString(AQT_GENERAL_HEALTH_CRITICAL);
    }

    if(!m_directoryOfServices){
        LOG_WARNING(m_id, "Detailed status requested and this module has no valid DOS object");
        val["directoryOfServices"].setString(AQT_GENERAL_HEALTH_ERROR);
        val["general"].setString(AQT_GENERAL_HEALTH_WARNING);
    }
    
    return val;
}

void AquetiDaemonServer::forceNtpSync()
{
    int rc = 0;
    std::string cmd1 = "service ntp stop";
    rc = std::system(cmd1.c_str());
    if (rc != 0) {
        LOG_ERROR(m_id, "Failed to stop ntp service");
    }
    std::string cmd2 = "ntpd -gqd";
    rc = std::system(cmd2.c_str());
    if (rc != 0) {
        LOG_ERROR(m_id, "Failed to force ntp sync");
    }
    std::string cmd3 = "service ntp start";
    rc = std::system(cmd3.c_str());
    if (rc != 0) {
        LOG_ERROR(m_id, "Failed to start ntp service");
    }
}

void AquetiDaemonServer::addNtpServer(std::vector<std::string> servers)
{
    int rc = 0;
    std::string cmd1 = "service ntp stop";
    rc = std::system(cmd1.c_str());
    if (rc != 0) {
        LOG_ERROR(m_id, "Failed to stop ntp service");
    }
    std::ofstream ntpfile;
    ntpfile.open("/etc/ntp.conf", std::ios_base::out);
    ntpfile << "tinker panic 0" << std::endl;
    ntpfile << "driftfile /var/lib/ntp/ntp.drift" << std::endl;
    //probably need to do some sort of check here
    for(std::string serv : servers){
        ntpfile << "server " << serv << std::endl;
    }
    std::string cmd3 = "service ntp start";
    rc = std::system(cmd3.c_str());
    if (rc != 0) {
        LOG_ERROR(m_id, "Failed to start ntp service");
    }
}

void AquetiDaemonServer::shutdownSystem()
{
    std::string cmd = "shutdown now";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        LOG_ERROR(m_id, "Failed to issue device shutdown command");
    }
}

void AquetiDaemonServer::restartSystem()
{
    std::string cmd = "reboot";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        LOG_ERROR(m_id, "Failed to issue device reboot command");
    }
}

bool AquetiDaemonServer::updateConfigFile(JsonBox::Value config)
{
    // if we have no config file specified, then we should abort
    if (m_configFile.empty()) {
        LOG_WARNING(m_id, "No config file specified, cannot update. This probably means the daemon is not running in the standard mode");
        return false;
    }

    // We write to a tmp file first, then move it to the actual config.
    // This prevents an issue where crashing while writing causes an 
    // empty or corrupted file.
    std::string tmpConfigFile = m_configFile + ".tmp";
    try {
        config.writeToFile(tmpConfigFile);
    } catch (...) {
        LOG_ERROR(m_id, "Failed to write temporary config file " + tmpConfigFile);
        return false;
    }
    // we must also check that the temporary file has non-zero size, since
    // a full disk may not cause JsonBox::writeToFile() to throw an error
    {
        struct stat stat_buf;
        int rc = stat(tmpConfigFile.c_str(), &stat_buf);
        if (rc != 0) {
            LOG_ERROR(m_id, "Failed to stat temporary config file size: " + tmpConfigFile);
            return false;
        } else if (stat_buf.st_size <= 0) {
            LOG_ERROR(m_id, "Failed to stat temporary config file size: " + tmpConfigFile);
            return false;
        }
    }
    std::string cmd = "mv " + tmpConfigFile + " " + m_configFile;
    int rc = std::system(cmd.c_str());
    if (rc == 0) {
        LOG_DEBUG(m_id, "Updated config file at " + m_configFile);
        return true;
    } else {
        LOG_ERROR(m_id, "Failed to update config file at " + m_configFile);
        return false;
    }
}

bool AquetiDaemonServer::updateSubmoduleConfig(aqt::SubmoduleDescriptor sd, JsonBox::Value val)
{
    std::lock_guard<std::mutex> l(m_configMutex);
    LOG_DEBUG(m_id, "Updating configuration of submodule " + sd.type + " to " + atl::jsonValueToString(val));
    JsonBox::Array smArray = m_configuration["submodule"].getArray();
    bool found = false;
    for(unsigned i = 0; i < smArray.size(); i++){
        if (smArray[i]["type"].getString() == sd.type) {
            smArray[i] = val;
            found = true;
            break; // because there should not be more than 1 of a type of submodule in the config
        }
    }
    if (!found) {
        LOG_ERROR(m_id, "Failed to find submodule in configuration: " + sd.print());
        return false;
    }
    m_configuration["submodule"].setArray(smArray);
    return updateConfigFile(m_configuration);
}


bool AquetiDaemonServer::doingOkay()
{
    return atl::Heartbeat::doingOkay() && m_doingOkay;
}


bool AquetiDaemonServer::setCriticalFailureFlag()
{
    LOG_CRITICAL(m_id, "Method called to indicate critical failure state, sending signal to restart daemon");
    m_doingOkay = false;
    return true;
}
