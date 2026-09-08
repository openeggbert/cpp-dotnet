// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include <string>

namespace System::ServiceModel {

    /**
     * @brief A binding for SOAP 1.1 over HTTP, interoperable with a basic-profile service.
     *
     * @note This binding is deliberately narrow: SOAP 1.1, document/literal wrapped, UTF-8, no
     * security and no session, which is what `basicHttpBinding` means with its own defaults and
     * the only shape this runtime encodes. A service configured for anything else -- WS-Security,
     * MTOM, SOAP 1.2 -- is not reachable through it, and the binding says so rather than
     * pretending to negotiate.
     */
    class BasicHttpBinding {
    public:
        /** @brief Constructs the binding. */
        BasicHttpBinding() = default;

        /**
         * @brief The binding's name.
         *
         * @return "BasicHttpBinding".
         */
        [[nodiscard]] const std::string& getNameProperty() const
        {
            static const std::string name = "BasicHttpBinding";
            return name;
        }

        /**
         * @brief How long an operation may take before it is abandoned.
         *
         * @return The timeout in milliseconds.
         */
        [[nodiscard]] int getSendTimeoutMillisecondsProperty() const { return sendTimeout_; }

        /**
         * @brief Sets how long an operation may take before it is abandoned.
         *
         * @param value The timeout in milliseconds.
         */
        void setSendTimeoutMillisecondsProperty(int value) { sendTimeout_ = value; }

    private:
        int sendTimeout_ = 60000;
    };

} // namespace System::ServiceModel
