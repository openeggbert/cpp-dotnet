// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include <string>

#include "System/Exception.hpp"

namespace System::ServiceModel {

    /**
     * @brief Thrown when a communication error occurs on a channel.
     *
     * The transport failed, the reply was not a well-formed envelope, or the service answered
     * something the contract does not describe. A fault the service raised deliberately is
     * `FaultException` instead, which derives from this.
     */
    class CommunicationException : public System::Exception {
    public:
        /**
         * @brief Constructs the exception.
         *
         * @param message What went wrong.
         */
        explicit CommunicationException(const std::string& message) : System::Exception(message) {}
    };

} // namespace System::ServiceModel
