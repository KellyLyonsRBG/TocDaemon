/******************************************************************************
 *
 * \file AquetiDaemonClient.hpp
 * \brief Remote interface to an AquetiDaemonServer. Provides remote access
 *      to information about its AquetiDaemonServer's local submodules.
 *      also provides a functional interface to its AquetiDaemonServer's
 *      device-level parameters/statistics.
 * \author Bryan David Maione
 *
 * Copyright Aqueti 2018
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 *****************************************************************************/
#pragma once

#include "atl/Logger/BaseLogger.hpp"

#include "module/BaseSubmodule.hpp"
#include "module/ClientSubmodule.hpp"
#include "AquetiDaemonInterface.hpp"

class AquetiDaemonClient : public atl::AquetiDaemonInterface
                         , public aqt::Client
                         , public aqt::BaseLogger
{
	public:
		AquetiDaemonClient(atl::PropertyManagerCreated connStruct);
		/***********************
		 * Submodule management
		 ***********************/
		/**
		 * @brief Gets the struct needed to connect to the submodule with the given ID
		 * @param submoduleID The ID of the desired submodule
		 * @return The requested connection struct. Evaluates to false in case of failure
		 **/
        atl::PropertyManagerCreated getSubmoduleConnStruct(aqt::SubmoduleDescriptor submoduleID);

        /**
         * @brief Gets a list of IDs for the submodules owned by this object's 
         *      corresponding AquetiDaemonServer
         * @param submoduleType An optional type parameter to filter the return list.
         *      An empty string does no filtering.
         * @return A vector of submodule IDs
         **/
        std::vector<aqt::SubmoduleDescriptor> getSubmoduleList(std::string submoduleType="");
        /**
         * @brief Tells the corresponding server of this daemon to initalize it directory of services to
         *      the server provided in connStruct
         * @param connection struct for the directory of services server that the daemon should connect to
         **/
        bool startDirectoryOfServices(atl::PropertyManagerCreated connStruct);

        //TODO add system maintenance methods (e.g. setNtpServer(), getHardwareStatus(), etc.)
        std::string getHostId() { return m_id; }
        std::string getSystemId() { return m_systemId; }
        atl::PropertyManagerCreated getDirectoryOfServicesConnection();

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

    private:
    	/**
    	 * ID that mirror the ID of the host server
    	 */
    	std::string m_id;
        std::string m_systemId;
        std::vector<atl::TokenPtr> m_tokenBag;
};

