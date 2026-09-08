// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#include "System/ServiceModel/Channels/SoapChannel.hpp"

#include <utility>

#include "System/Net/Http/HttpClient.hpp"
#include "System/Net/Http/HttpMethod.hpp"
#include "System/Net/Http/HttpRequestMessage.hpp"
#include "System/Net/Http/HttpResponseMessage.hpp"
#include "System/Net/Http/StringContent.hpp"
#include "System/ServiceModel/Channels/SoapMessageEncoder.hpp"
#include "System/ServiceModel/CommunicationException.hpp"

namespace System::ServiceModel::Channels {

SoapChannel::SoapChannel(const BasicHttpBinding& binding, const EndpointAddress& remoteAddress,
                         std::string contractNamespace, std::string contractName)
    : binding_(binding),
      remoteAddress_(remoteAddress),
      contractNamespace_(std::move(contractNamespace)),
      contractName_(std::move(contractName)),
      httpClient_(std::make_shared<System::Net::Http::HttpClient>())
{
}

SoapChannel::~SoapChannel() = default;

std::optional<std::string> SoapChannel::Invoke(
    const std::string& operation,
    const std::function<void(System::Xml::XmlWriter&)>& writeParameters)
{
    const std::string envelope =
        SoapMessageEncoder::WriteRequest(operation, contractNamespace_, writeParameters);

    auto request = std::make_shared<System::Net::Http::HttpRequestMessage>(
        System::Net::Http::HttpMethod::Post(), remoteAddress_.getUriProperty().ToString());

    // The action is quoted in the header, as SOAP 1.1 requires; a service that routes on it
    // will not match an unquoted one.
    request->setHeader("SOAPAction",
                       "\"" + SoapMessageEncoder::ActionFor(contractNamespace_, contractName_,
                                                            operation) + "\"");
    request->setContentProperty(std::make_shared<System::Net::Http::StringContent>(
        envelope, nullptr, SoapMessageEncoder::getMediaTypeProperty()));

    std::shared_ptr<System::Net::Http::HttpResponseMessage> response;
    try {
        response = httpClient_->Send(request);
    } catch (const CommunicationException&) {
        throw;
    } catch (const std::exception& e) {
        throw CommunicationException(std::string("The request to ") +
                                     remoteAddress_.getUriProperty().ToString() + " failed: " +
                                     e.what());
    }

    if (response == nullptr || response->getContentProperty() == nullptr) {
        throw CommunicationException("The service returned no body.");
    }

    // A SOAP fault arrives with status 500, so the body is read and classified before the
    // status is judged -- refusing on the status alone would turn every fault into a
    // transport error and lose the reason the service gave.
    return SoapMessageEncoder::ReadResult(response->getContentProperty()->ReadAsString(),
                                          operation);
}

} // namespace System::ServiceModel::Channels
