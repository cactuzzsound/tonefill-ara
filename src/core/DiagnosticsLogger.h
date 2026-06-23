#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace tonefill::core
{
// Central sink for user-facing warnings and developer metrics.
// Threading: methods are safe to call from any thread (guarded). The UI drains
// snapshot() on the message thread via a timer; nothing here blocks DSP for long.
//
// Warnings map to the design's warning states (insufficient material, speech
// contamination, loop risk, sample-rate mismatch, host unsupported, ...).
class DiagnosticsLogger
{
public:
    enum class Severity { Info, Warning, Error };

    struct Entry
    {
        Severity    severity { Severity::Info };
        std::string code;        // stable machine code, e.g. "INSUFFICIENT_MATERIAL"
        std::string message;     // human-readable
    };

    void log (Severity severity, std::string code, std::string message);

    void info    (std::string code, std::string message) { log (Severity::Info,    std::move (code), std::move (message)); }
    void warn    (std::string code, std::string message) { log (Severity::Warning, std::move (code), std::move (message)); }
    void error   (std::string code, std::string message) { log (Severity::Error,   std::move (code), std::move (message)); }

    // Returns and clears the accumulated entries (message-thread drain).
    std::vector<Entry> snapshot();

    void clear();

    // Optional live hook (e.g. to forward to JUCE's Logger during development).
    void setListener (std::function<void (const Entry&)> listener);

private:
    std::mutex                          mutex_;
    std::vector<Entry>                  entries_;
    std::function<void (const Entry&)>  listener_;
};
} // namespace tonefill::core
