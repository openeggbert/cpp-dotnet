// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "System/Xml/XmlWriter.hpp"

namespace System::ServiceModel::Channels {

    /**
     * @brief Turns an operation call into a SOAP 1.1 envelope, and a reply back into a result.
     *
     * The shape is document/literal *wrapped*, which is what `basicHttpBinding` produces: the
     * operation is one element named after itself, in the contract's namespace, and its
     * parameters are child elements named after the parameters, in declaration order. The reply
     * mirrors it -- `<OpResponse>` wrapping `<OpResult>`.
     *
     * @note The encoder does not know the contract. It cannot: a wrapped call carries no type
     * information, so which elements to write and how to read the result back are the caller's
     * knowledge, exactly as they are a generated proxy's. What this class owns is the envelope
     * around them, which is the part every operation shares.
     */
    class SoapMessageEncoder {
    public:
        /**
         * @brief The SOAP 1.1 envelope namespace.
         *
         * @return `http://schemas.xmlsoap.org/soap/envelope/`.
         */
        [[nodiscard]] static const std::string& getEnvelopeNamespaceProperty();

        /**
         * @brief The media type a SOAP 1.1 request is sent as.
         *
         * @return `text/xml`.
         */
        [[nodiscard]] static const std::string& getMediaTypeProperty();

        /**
         * @brief Builds the request envelope for one operation.
         *
         * @param operation         The operation's name.
         * @param contractNamespace The contract's namespace, which the operation element
         *                          carries as its default namespace.
         * @param writeParameters   Writes the parameter elements into the operation element.
         *                          It receives a writer already positioned inside it.
         * @return The complete envelope, ready to be sent as the request body.
         */
        [[nodiscard]] static std::string WriteRequest(
            const std::string& operation, const std::string& contractNamespace,
            const std::function<void(System::Xml::XmlWriter&)>& writeParameters);

        /**
         * @brief The SOAPAction header value for an operation.
         *
         * @param contractNamespace The contract's namespace.
         * @param contractName      The contract's name, such as `IYachtService`.
         * @param operation         The operation's name.
         * @return The action URI, unquoted.
         */
        [[nodiscard]] static std::string ActionFor(const std::string& contractNamespace,
                                                   const std::string& contractName,
                                                   const std::string& operation);

        /**
         * @brief Reads an operation's result out of a reply envelope.
         *
         * @param envelope  The reply's body.
         * @param operation The operation's name, which names the elements to look for.
         * @return The text of `<OpResult>`, or nothing when the reply carries no result
         *         element -- which is both a void operation and a null return, because the
         *         wire does not distinguish them.
         *
         * @throws System::ServiceModel::FaultException when the reply is a SOAP fault.
         * @throws System::ServiceModel::CommunicationException when it is not an envelope at
         *         all, or carries a different operation's response.
         */
        [[nodiscard]] static std::optional<std::string> ReadResult(const std::string& envelope,
                                                                   const std::string& operation);
    };

} // namespace System::ServiceModel::Channels
