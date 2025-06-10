#include "TocHeartbeat.h"
#include <chrono>

Heartbeat::Heartbeat(double timeOut)
{
    m_timeOut = timeOut;
    m_timer = std::chrono::steady_clock::now();
}

bool Heartbeat::doingOkay()
{
    auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
        std::chrono::steady_clock::now() - m_timer).count();
    return elapsed <= m_timeOut;
}

void Heartbeat::pulse()
{
    m_timer = std::chrono::steady_clock::now();
}

