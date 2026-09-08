// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "System/ServiceModel/BasicHttpBinding.hpp"
#include "System/ServiceModel/EndpointAddress.hpp"
#include "System/Xml/XmlWriter.hpp"

namespace System::Net::Http {
    class HttpClient;
}

namespace System::ServiceModel::Channels {

    /**
     * @brief Sends SOAP 1.1 operations over HTTP and hands back their results.
     *
     * One channel per endpoint. It owns an HTTP client, so a proxy that keeps a channel keeps
     * the connection habits with it rather than building a new client per call.
     */
    class SoapChannel {
    public:
        /**
         * @brief Opens a channel to an endpoint.
         *
         * @param binding           The binding; only its timeout is consulted, because SOAP 1.1
         *                          over HTTP is the only shape this channel speaks.
         * @param remoteAddress     Where the service is.
         * @param contractNamespace The contract's namespace, such as `http://tempuri.org/`.
         * @param contractName      The contract's name, such as `IYachtService`.
         */
        SoapChannel(const BasicHttpBinding& binding, const EndpointAddress& remoteAddress,
                    std::string contractNamespace, std::string contractName);

        /** @brief Closes the channel. */
        ~SoapChannel();

        SoapChannel(const SoapChannel&) = delete;
        SoapChannel& operator=(const SoapChannel&) = delete;

        /**
         * @brief Calls one operation and returns its result.
         *
         * @param operation       The operation's name.
         * @param writeParameters Writes the parameter elements. May be null for an operation
         *                        that takes none.
         * @return The result element's text, or nothing for a void operation.
         *
         * @throws System::ServiceModel::FaultException when the service refuses.
         * @throws System::ServiceModel::CommunicationException when the transport fails or the
         *         reply is not a well-formed answer to this operation.
         */
        std::optional<std::string> Invoke(
            const std::string& operation,
            const std::function<void(System::Xml::XmlWriter&)>& writeParameters);

        /**
         * @brief The endpoint this channel talks to.
         *
         * @return The address.
         */
        [[nodiscard]] const EndpointAddress& getRemoteAddressProperty() const
        {
            return remoteAddress_;
        }

    private:
        BasicHttpBinding binding_;
        EndpointAddress remoteAddress_;
        std::string contractNamespace_;
        std::string contractName_;
        std::shared_ptr<System::Net::Http::HttpClient> httpClient_;
    };

} // namespace System::ServiceModel::Channels
