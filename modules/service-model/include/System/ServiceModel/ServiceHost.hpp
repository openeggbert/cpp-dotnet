// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "System/ServiceModel/EndpointAddress.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"

namespace System::ServiceModel {

    /**
     * @brief Hosts a service so clients can call its operations over SOAP 1.1.
     *
     * @note **The operations are a table the service fills in, not a type the host reflects
     * over.** .NET's ServiceHost takes a service type, finds its `[OperationContract]` methods
     * and dispatches by name. C++ cannot ask a type for its methods, so a service registers each
     * operation with `AddOperation` -- the same explicit-table shape every other reflection-free
     * corner of this runtime takes. An operation the table does not know is answered with a
     * fault, which is what .NET does with an action it cannot route.
     *
     * @note The metadata endpoint is served only when a service supplies it. .NET generates WSDL
     * from the contract's types; nothing here can, so `SetMetadata` takes the documents the
     * service wants published and the host answers `?wsdl` and `?xsd=...` from them. A host
     * given none answers those with 404 rather than an empty document that a client would take
     * for a contract.
     */
    class ServiceHost {
    public:
        /**
         * @brief What one operation does.
         *
         * @param parameters A reader positioned inside the operation element, on its first
         *                   parameter.
         * @return The text of the operation's result, or nothing when it returns void.
         */
        using OperationHandler =
            std::function<std::optional<std::string>(System::Xml::XmlReader& parameters)>;

        /**
         * @brief Creates a host for one endpoint.
         *
         * @param baseAddress       Where the service listens.
         * @param contractNamespace The contract's namespace, such as `http://tempuri.org/`.
         * @param contractName      The contract's name, such as `IYachtService`.
         */
        ServiceHost(const EndpointAddress& baseAddress, std::string contractNamespace,
                    std::string contractName);

        /** @brief Closes the host if it is open. */
        ~ServiceHost();

        ServiceHost(const ServiceHost&) = delete;
        ServiceHost& operator=(const ServiceHost&) = delete;

        /**
         * @brief Registers what one operation does.
         *
         * @param operation The operation's name, as it appears in the envelope.
         * @param handler   What to run.
         */
        void AddOperation(const std::string& operation, OperationHandler handler);

        /**
         * @brief Supplies the metadata documents to publish.
         *
         * @param wsdl    The service description, answered at `?wsdl`.
         * @param schemas The schemas, keyed by the name they are asked for -- `xsd0`, `xsd1`.
         */
        void SetMetadata(std::string wsdl, std::map<std::string, std::string> schemas);

        /** @brief Starts listening. */
        void Open();

        /** @brief Stops listening and waits for the listener to finish. */
        void Close();

        /**
         * @brief Whether the host is listening.
         *
         * @return True while open.
         */
        [[nodiscard]] bool getIsOpenProperty() const { return open_; }

        /**
         * @brief Where the service listens.
         *
         * @return The base address.
         */
        [[nodiscard]] const EndpointAddress& getBaseAddressProperty() const { return baseAddress_; }

    private:
        void Listen();
        [[nodiscard]] std::string HandleRequest(const std::string& requestLine,
                                                const std::string& soapAction,
                                                const std::string& body);

        EndpointAddress baseAddress_;
        std::string contractNamespace_;
        std::string contractName_;

        std::map<std::string, OperationHandler> operations_;
        std::string wsdl_;
        std::map<std::string, std::string> schemas_;

        std::atomic<bool> open_{false};
        std::thread listener_;
        int listenSocket_ = -1;
        std::mutex mutex_;
    };

} // namespace System::ServiceModel
