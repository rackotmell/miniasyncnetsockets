/**
 * @file errors.hpp
 * @brief Exception hierarchy for miniasyncnetsockets.
 */

#pragma once

#include <stdexcept>

namespace miniasyncnetsockets
{

/**
 * @brief Base class for all miniasyncnetsockets errors.
 */
class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief Thrown when an object is used in an invalid lifecycle state.
 */
class InvalidState : public Error
{
public:
    using Error::Error;
};

/**
 * @brief Thrown for framing protocol violations.
 */
class ProtocolError : public Error
{
public:
    using Error::Error;
};

/**
 * @brief Thrown when a frame payload exceeds the configured maximum size.
 */
class FrameTooLarge : public ProtocolError
{
public:
    using ProtocolError::ProtocolError;
};

/**
 * @brief Thrown when the pending write queue exceeds its byte limit.
 */
class WriteQueueOverflow : public Error
{
public:
    using Error::Error;
};

} // namespace miniasyncnetsockets
