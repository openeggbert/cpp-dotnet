// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#include "System/ServiceModel/Channels/SoapMessageEncoder.hpp"

#include <memory>

#include "System/ServiceModel/CommunicationException.hpp"
#include "System/ServiceModel/FaultException.hpp"
#include "System/Xml/XmlNodeType.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriterSettings.hpp"

namespace System::ServiceModel::Channels {

namespace {

// The envelope is fixed boilerplate and the operation element is the only variable part, so
// the writer is used for the part that needs escaping and the rest is literal. Writing the
// whole envelope through the writer would mean spelling prefixed names and xmlns attributes
// past its name checks for no gain, since none of the boilerplate can vary.
constexpr const char* kEnvelopeOpen =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>";
constexpr const char* kEnvelopeClose = "</s:Body></s:Envelope>";

std::string LocalName(const std::string& name)
{
    const std::size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

} // namespace

const std::string& SoapMessageEncoder::getEnvelopeNamespaceProperty()
{
    static const std::string value = "http://schemas.xmlsoap.org/soap/envelope/";
    return value;
}

const std::string& SoapMessageEncoder::getMediaTypeProperty()
{
    static const std::string value = "text/xml";
    return value;
}

std::string SoapMessageEncoder::WriteRequest(
    const std::string& operation, const std::string& contractNamespace,
    const std::function<void(System::Xml::XmlWriter&)>& writeParameters)
{
    System::Xml::XmlWriterSettings settings;
    settings.OmitXmlDeclaration = true;

    std::unique_ptr<System::Xml::XmlWriter> writer(
        System::Xml::XmlWriter::CreateToString(settings));

    writer->WriteStartElement(operation);
    writer->WriteAttributeString("xmlns", contractNamespace);
    if (writeParameters) {
        writeParameters(*writer);
    }
    writer->WriteFullEndElement();

    return kEnvelopeOpen + writer->ToString() + kEnvelopeClose;
}

std::string SoapMessageEncoder::ActionFor(const std::string& contractNamespace,
                                          const std::string& contractName,
                                          const std::string& operation)
{
    std::string action = contractNamespace;
    if (!action.empty() && action.back() != '/') {
        action.push_back('/');
    }
    return action + contractName + "/" + operation;
}

std::optional<std::string> SoapMessageEncoder::ReadResult(const std::string& envelope,
                                                          const std::string& operation)
{
    std::unique_ptr<System::Xml::XmlReader> reader;
    try {
        reader.reset(System::Xml::XmlReader::CreateFromString(envelope));
    } catch (const std::exception& e) {
        throw CommunicationException(std::string("The reply is not XML: ") + e.what());
    }

    const std::string responseElement = operation + "Response";
    const std::string resultElement = operation + "Result";

    // A fault carries the reason the service refused, and is answered with the same 500 status
    // as a transport failure -- so it has to be recognised here rather than by status code.
    bool sawFault = false;
    std::string faultCode;
    std::string faultReason;
    bool sawResponse = false;

    while (reader->Read()) {
        if (reader->getNodeTypeProperty() != System::Xml::XmlNodeType::Element) {
            continue;
        }

        const std::string name = LocalName(reader->getNameProperty());

        if (name == "Fault") {
            sawFault = true;
            continue;
        }
        if (sawFault && (name == "faultcode" || name == "Code" || name == "Value")) {
            if (reader->Read() && faultCode.empty()) {
                faultCode = reader->getValueProperty();
            }
            continue;
        }
        if (sawFault && (name == "faultstring" || name == "Text" || name == "Reason")) {
            if (reader->Read() && faultReason.empty()) {
                faultReason = reader->getValueProperty();
            }
            continue;
        }
        if (name == responseElement) {
            sawResponse = true;
            continue;
        }
        if (sawResponse && name == resultElement) {
            // An empty result element carries an empty string, not "no result": the wire
            // distinguishes <R></R> from a missing <R> and so does this.
            if (reader->getIsEmptyElementProperty()) {
                return std::string();
            }
            if (!reader->Read()) {
                return std::string();
            }
            if (reader->getNodeTypeProperty() == System::Xml::XmlNodeType::EndElement) {
                return std::string();
            }
            return reader->getValueProperty();
        }
    }

    if (sawFault) {
        throw FaultException(faultReason.empty() ? "The service returned a fault." : faultReason,
                             faultCode);
    }
    if (!sawResponse) {
        throw CommunicationException("The reply carries no " + responseElement + " element.");
    }

    // The response element was there and the result element was not: a void operation, or a
    // null return. The contract, not the envelope, says which.
    return std::nullopt;
}

} // namespace System::ServiceModel::Channels
