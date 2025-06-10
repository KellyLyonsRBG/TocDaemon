/******************************************************************************
 *
 * \file AtlHeartbeat.hpp
 * \brief This class encompasses basic heartbeat functionality, any class that
 *      needs to be monitored should inherit from this class. If the class fails
 *      to call the pulse function before the timeout it will get flagged for
 *      deletion.
 * \author Bryan David Maione
 *
 * Copyright Aqueti 2018
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 *****************************************************************************/
#pragma once

#include <chrono>


class Heartbeat
{
    public:
        /**
        * @brief Constructor for the heartbeat object, 
        *   requires a timeout in seconds.
        * @param timeOut the maximum duration in seconds 
        *   allowed without a pulse() call
        **/
        Heartbeat(double timeOut);

        /**
         * @brief Virtual function used to signify if this object is doing okay,
         *      by default, this method will return true of the current timer time
         *      is less than m_timeOut. This method can be overwritten to signify 
         *      the health of an object that may not be threaded to call pulse();
         * @return returns true if doing okay.
         *  
         **/
        virtual bool doingOkay();
        /**
         * @brief Resets the timer and keeps doingOkay true when called periodically
         **/
        void pulse();

    private:
        /**
         * @brief Time in seconds before the heartbeat times out
         **/
        double m_timeOut;

        /**
         * @brief ATL timer object
         **/
        std::chrono::steady_clock::time_point m_timer;
};

