#include "Logger.h"

//
// Created by maxim on 26.08.2024.
//
void AsyncLogger::loggerThreadFunction()
{
    while (true) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_condition.wait(lock, [this] { return !m_logQueue.empty() || m_stopFlag; });

        if (m_stopFlag && m_logQueue.empty()) {
            break;
        }

        LogEntry entry = std::move(m_logQueue.front());
        m_logQueue.pop();
        lock.unlock();

        std::lock_guard<std::mutex> destLock(m_destinationsMutex);
        for (const auto& dest : m_destinations) {
            dest->write(entry);
        }
    }
}
