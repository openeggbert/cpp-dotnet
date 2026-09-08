// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include <string>

#include "System/ServiceModel/CommunicationException.hpp"

namespace System::ServiceModel {

    /**
     * @brief Thrown when a service returns a SOAP fault.
     *
     * A fault is the service's considered answer, not a transport failure: it reached the
     * service, the service understood it, and the service refused. The reason string is the
     * one the service sent.
     */
    class FaultException : public CommunicationException {
    public:
        /**
         * @brief Constructs the exception.
         *
         * @param reason The fault's reason, as the service worded it.
         * @param code   The fault code, such as `s:Client` or `s:Server`.
         */
        FaultException(const std::string& reason, std::string code)
            : CommunicationException(reason), code_(std::move(code))
        {
        }

        /**
         * @brief The fault code the service sent.
         *
         * @return The code, such as `s:Client`.
         */
        [[nodiscard]] const std::string& getCodeProperty() const { return code_; }

    private:
        std::string code_;
    };

} // namespace System::ServiceModel
