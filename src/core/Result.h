#pragma once

#include <string>
#include <utility>
#include <variant>

namespace tonefill::core
{
// Lightweight success/failure carrier passed across worker boundaries so DSP/engine
// code never throws into ARA or host callbacks. Plain value type, thread-agnostic.
enum class Status
{
    Ok,
    Cancelled,
    InvalidInput,
    InsufficientMaterial,
    HostUnsupported,
    Internal
};

struct Error
{
    Status      status { Status::Internal };
    std::string message;
};

template <typename T>
class Result
{
public:
    Result (T value) : payload_ (std::move (value)) {}        // NOLINT(google-explicit-constructor)
    Result (Error err) : payload_ (std::move (err)) {}        // NOLINT(google-explicit-constructor)

    static Result fail (Status s, std::string msg) { return Result (Error { s, std::move (msg) }); }

    bool ok() const noexcept              { return std::holds_alternative<T> (payload_); }
    explicit operator bool() const        { return ok(); }

    const T&     value() const            { return std::get<T> (payload_); }
    T&           value()                  { return std::get<T> (payload_); }
    const Error& error() const            { return std::get<Error> (payload_); }

private:
    std::variant<T, Error> payload_;
};
} // namespace tonefill::core
