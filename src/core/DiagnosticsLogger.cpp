#include "core/DiagnosticsLogger.h"

namespace tonefill::core
{
void DiagnosticsLogger::log (Severity severity, std::string code, std::string message)
{
    Entry entry { severity, std::move (code), std::move (message) };

    std::function<void (const Entry&)> listenerCopy;
    {
        std::lock_guard<std::mutex> lock (mutex_);
        entries_.push_back (entry);
        listenerCopy = listener_;
    }

    // Invoke the listener outside the lock to avoid re-entrancy deadlocks.
    if (listenerCopy)
        listenerCopy (entry);
}

std::vector<DiagnosticsLogger::Entry> DiagnosticsLogger::snapshot()
{
    std::lock_guard<std::mutex> lock (mutex_);
    std::vector<Entry> out;
    out.swap (entries_);
    return out;
}

void DiagnosticsLogger::clear()
{
    std::lock_guard<std::mutex> lock (mutex_);
    entries_.clear();
}

void DiagnosticsLogger::setListener (std::function<void (const Entry&)> listener)
{
    std::lock_guard<std::mutex> lock (mutex_);
    listener_ = std::move (listener);
}
} // namespace tonefill::core
