#pragma once

#include <stdexcept>

namespace miniasyncnetsockets
{

class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class InvalidState : public Error
{
public:
    using Error::Error;
};

class ProtocolError : public Error
{
public:
    using Error::Error;
};

class FrameTooLarge : public ProtocolError
{
public:
    using ProtocolError::ProtocolError;
};

class WriteQueueOverflow : public Error
{
public:
    using Error::Error;
};

} // namespace miniasyncnetsockets
