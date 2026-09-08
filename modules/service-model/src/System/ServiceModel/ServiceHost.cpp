// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#include "System/ServiceModel/ServiceHost.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#if defined(_WIN32)
#    include <winsock2.h>
#    include <ws2tcpip.h>
using socket_t = SOCKET;
#    define SM_CLOSE_SOCKET closesocket
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
using socket_t = int;
#    define SM_CLOSE_SOCKET ::close
#endif

#include "System/ServiceModel/Channels/SoapMessageEncoder.hpp"
#include "System/ServiceModel/CommunicationException.hpp"
#include "System/Xml/XmlNodeType.hpp"

namespace System::ServiceModel {

namespace {

std::string LocalName(const std::string& name)
{
    const std::size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

std::string HttpResponse(const std::string& status, const std::string& contentType,
                         const std::string& body)
{
    return "HTTP/1.1 " + status + "\r\nContent-Type: " + contentType +
           "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" +
           body;
}

// The response envelope is assembled as text, so anything a handler returns has to be escaped
// on the way in. Without this a result carrying an ampersand or an angle bracket -- a player's
// name, a game's name -- produces an envelope the client cannot parse, and the failure looks
// like a transport error rather than what it is.
std::string EscapeXmlText(const std::string& text)
{
    std::string escaped;
    escaped.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

std::string Fault(const std::string& code, const std::string& reason)
{
    const std::string body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<s:Fault><faultcode>" +
        EscapeXmlText(code) + "</faultcode><faultstring>" + EscapeXmlText(reason) +
        "</faultstring></s:Fault>"
        "</s:Body></s:Envelope>";
    // A SOAP 1.1 fault travels with 500, which is why a client has to read the body before it
    // judges the status.
    return HttpResponse("500 Internal Server Error", "text/xml; charset=utf-8", body);
}

// Reads a whole request off a connected socket: the request line, the SOAPAction header and
// the body. Understands a Content-Length body and nothing else, deliberately -- see the
// header's note on the same choice in the notification channel.
bool ReadRequest(socket_t connection, std::string& requestLine, std::string& soapAction,
                 std::string& body)
{
    std::string buffer;
    char chunk[4096];
    std::size_t headerEnd = std::string::npos;

    while (headerEnd == std::string::npos) {
        const auto read = ::recv(connection, chunk, sizeof(chunk), 0);
        if (read <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<std::size_t>(read));
        headerEnd = buffer.find("\r\n\r\n");
        if (buffer.size() > (4u << 20)) {
            return false;
        }
    }

    const std::string headers = buffer.substr(0, headerEnd);
    const std::size_t firstLineEnd = headers.find("\r\n");
    requestLine = headers.substr(0, firstLineEnd == std::string::npos ? headers.size()
                                                                     : firstLineEnd);

    std::string lowered = headers;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::size_t contentLength = 0;
    if (const std::size_t at = lowered.find("content-length:"); at != std::string::npos) {
        contentLength = static_cast<std::size_t>(
            std::strtoul(headers.c_str() + at + std::strlen("content-length:"), nullptr, 10));
    }

    if (const std::size_t at = lowered.find("soapaction:"); at != std::string::npos) {
        const std::size_t valueAt = at + std::strlen("soapaction:");
        const std::size_t lineEnd = headers.find("\r\n", valueAt);
        soapAction = headers.substr(valueAt, lineEnd == std::string::npos ? std::string::npos
                                                                         : lineEnd - valueAt);
        // The action is quoted and may be padded; both are the sender's business, not ours.
        soapAction.erase(0, soapAction.find_first_not_of(" \t\""));
        if (const std::size_t end = soapAction.find_last_not_of(" \t\"");
            end != std::string::npos) {
            soapAction.erase(end + 1);
        }
    }

    body = buffer.substr(headerEnd + 4);
    while (body.size() < contentLength) {
        const auto read = ::recv(connection, chunk, sizeof(chunk), 0);
        if (read <= 0) {
            break;
        }
        body.append(chunk, static_cast<std::size_t>(read));
    }

    return true;
}

} // namespace

ServiceHost::ServiceHost(const EndpointAddress& baseAddress, std::string contractNamespace,
                         std::string contractName)
    : baseAddress_(baseAddress),
      contractNamespace_(std::move(contractNamespace)),
      contractName_(std::move(contractName))
{
}

ServiceHost::~ServiceHost()
{
    Close();
}

void ServiceHost::AddOperation(const std::string& operation, OperationHandler handler)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    operations_[operation] = std::move(handler);
}

void ServiceHost::SetMetadata(std::string wsdl, std::map<std::string, std::string> schemas)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    wsdl_ = std::move(wsdl);
    schemas_ = std::move(schemas);
}

void ServiceHost::Open()
{
    if (open_) {
        return;
    }

    // The port comes out of the base address, which is what a .NET host reads from its
    // configuration; everything else about the address is the client's concern.
    const std::string uri = baseAddress_.getUriProperty().ToString();
    int port = 80;
    if (const std::size_t hostAt = uri.find("//"); hostAt != std::string::npos) {
        if (const std::size_t portAt = uri.find(':', hostAt + 2); portAt != std::string::npos) {
            port = std::atoi(uri.c_str() + portAt + 1);
        }
    }

    const socket_t listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        throw CommunicationException("ServiceHost: could not create a listening socket.");
    }

    int reuse = 1;
    (void)::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                       sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port));

    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 16) != 0) {
        SM_CLOSE_SOCKET(listener);
        throw CommunicationException("ServiceHost: could not listen on " + uri + ".");
    }

    listenSocket_ = static_cast<int>(listener);
    open_ = true;
    listener_ = std::thread([this] { Listen(); });
}

void ServiceHost::Listen()
{
    while (open_) {
        const socket_t connection = ::accept(static_cast<socket_t>(listenSocket_), nullptr, nullptr);
        if (connection < 0) {
            if (!open_) {
                break;
            }
            continue;
        }

        std::string requestLine;
        std::string soapAction;
        std::string body;
        std::string response;

        if (ReadRequest(connection, requestLine, soapAction, body)) {
            response = HandleRequest(requestLine, soapAction, body);
        } else {
            response = HttpResponse("400 Bad Request", "text/plain", "");
        }

        (void)::send(connection, response.c_str(), response.size(), 0);
        SM_CLOSE_SOCKET(connection);
    }
}

std::string ServiceHost::HandleRequest(const std::string& requestLine,
                                       const std::string& soapAction, const std::string& body)
{
    // Metadata first: a GET with ?wsdl or ?xsd= is how a client discovers the contract, and it
    // is answered from what the service published rather than generated.
    if (requestLine.rfind("GET ", 0) == 0) {
        const std::lock_guard<std::mutex> lock(mutex_);

        if (requestLine.find("?wsdl") != std::string::npos && !wsdl_.empty()) {
            return HttpResponse("200 OK", "text/xml; charset=utf-8", wsdl_);
        }

        if (const std::size_t at = requestLine.find("?xsd="); at != std::string::npos) {
            const std::size_t nameAt = at + std::strlen("?xsd=");
            const std::size_t nameEnd = requestLine.find_first_of(" &", nameAt);
            const std::string name = requestLine.substr(
                nameAt, nameEnd == std::string::npos ? std::string::npos : nameEnd - nameAt);
            if (const auto found = schemas_.find(name); found != schemas_.end()) {
                return HttpResponse("200 OK", "text/xml; charset=utf-8", found->second);
            }
        }

        return HttpResponse("404 Not Found", "text/plain", "");
    }

    // The operation is named by the body's first element, not by the action: the action is a
    // routing hint a sender may quote, pad or omit, and the envelope is the request itself.
    std::unique_ptr<System::Xml::XmlReader> reader;
    try {
        reader.reset(System::Xml::XmlReader::CreateFromString(body));
    } catch (const std::exception&) {
        return Fault("s:Client", "The request is not XML.");
    }

    std::string operation;
    while (reader->Read()) {
        if (reader->getNodeTypeProperty() != System::Xml::XmlNodeType::Element) {
            continue;
        }
        const std::string name = LocalName(reader->getNameProperty());
        if (name == "Envelope" || name == "Body" || name == "Header") {
            continue;
        }
        operation = name;
        break;
    }

    if (operation.empty()) {
        return Fault("s:Client", "The request carries no operation.");
    }

    OperationHandler handler;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (const auto found = operations_.find(operation); found != operations_.end()) {
            handler = found->second;
        }
    }

    if (!handler) {
        return Fault("s:Client", "The service has no operation named " + operation + ".");
    }

    std::optional<std::string> result;
    try {
        result = handler(*reader);
    } catch (const std::exception& e) {
        return Fault("s:Server", e.what());
    } catch (...) {
        return Fault("s:Server", "The operation failed.");
    }

    std::string response =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<" + operation + "Response xmlns=\"" + contractNamespace_ + "\">";
    if (result.has_value()) {
        response += "<" + operation + "Result>" + EscapeXmlText(*result) + "</" +
                    operation + "Result>";
    }
    response += "</" + operation + "Response></s:Body></s:Envelope>";

    (void)soapAction;
    return HttpResponse("200 OK", "text/xml; charset=utf-8", response);
}

void ServiceHost::Close()
{
    if (!open_) {
        return;
    }

    open_ = false;

    if (listenSocket_ >= 0) {
#if defined(_WIN32)
        ::shutdown(static_cast<socket_t>(listenSocket_), SD_BOTH);
#else
        ::shutdown(static_cast<socket_t>(listenSocket_), SHUT_RDWR);
#endif
        SM_CLOSE_SOCKET(static_cast<socket_t>(listenSocket_));
        listenSocket_ = -1;
    }

    if (listener_.joinable()) {
        listener_.join();
    }
}

} // namespace System::ServiceModel
