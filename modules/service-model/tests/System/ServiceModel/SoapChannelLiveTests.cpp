// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
//
// A live round trip against a real WCF service.
//
// Encoding tests prove the bytes are the ones a service was seen to accept; only this proves
// the whole path -- envelope, SOAPAction header, content type, transport and reply parsing --
// against a service that is running now. It is skipped unless SHARP_RUNTIME_SOAP_ENDPOINT names
// one, because a suite that requires a server is a suite that fails on a machine without it.
//
// The service it was developed against is SAMPLE-071's unchanged original:
//
//     cd /rv/tmp/samples/SAMPLE-071-Yacht_4_0/xna4-build/bin
//     mkfifo /tmp/srvin, hold it open with a sleep, then: mono Server.exe < /tmp/srvin
//     SHARP_RUNTIME_SOAP_ENDPOINT=http://localhost:8888/GameServer/ ./SharpRuntimeTests_ServiceModel
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "System/Convert.hpp"
#include "System/ServiceModel/BasicHttpBinding.hpp"
#include "System/ServiceModel/Channels/SoapChannel.hpp"
#include "System/ServiceModel/EndpointAddress.hpp"
#include "System/Xml/XmlWriter.hpp"

using System::ServiceModel::BasicHttpBinding;
using System::ServiceModel::EndpointAddress;
using System::ServiceModel::Channels::SoapChannel;
using System::Xml::XmlWriter;

namespace {

std::string Endpoint()
{
    const char* value = std::getenv("SHARP_RUNTIME_SOAP_ENDPOINT");
    return value == nullptr ? std::string() : std::string(value);
}

} // namespace

TEST(SoapChannelLiveTest, RegistersWithARealServiceAndGetsASessionBack)
{
    const std::string endpoint = Endpoint();
    if (endpoint.empty()) {
        GTEST_SKIP() << "SHARP_RUNTIME_SOAP_ENDPOINT is not set";
    }

    SoapChannel channel(BasicHttpBinding(), EndpointAddress(endpoint), "http://tempuri.org/",
                        "IYachtService");

    const auto session = channel.Invoke("Register", [](XmlWriter& writer) {
        writer.WriteElementString("clientURI", "http://127.0.0.1:9999/push/");
        writer.WriteElementString("name", "Kolatt");
        writer.WriteElementString("playerID", "-1");
    });

    ASSERT_TRUE(session.has_value());
    EXPECT_GT(std::stoi(*session), 0);
}

TEST(SoapChannelLiveTest, CreatesAGameAndThenSeesItInTheAvailableList)
{
    const std::string endpoint = Endpoint();
    if (endpoint.empty()) {
        GTEST_SKIP() << "SHARP_RUNTIME_SOAP_ENDPOINT is not set";
    }

    SoapChannel channel(BasicHttpBinding(), EndpointAddress(endpoint), "http://tempuri.org/",
                        "IYachtService");

    const auto session = channel.Invoke("Register", [](XmlWriter& writer) {
        writer.WriteElementString("clientURI", "http://127.0.0.1:9999/push/");
        writer.WriteElementString("name", "Live");
        writer.WriteElementString("playerID", "-1");
    });
    ASSERT_TRUE(session.has_value());
    const std::string sessionID = *session;

    // A game name has to be unique to the service, so it carries the session that made it.
    const std::string gameName = "G" + sessionID;

    const auto created = channel.Invoke("NewGame", [&](XmlWriter& writer) {
        writer.WriteElementString("sessionID", sessionID);
        writer.WriteElementString("name", gameName);
    });
    ASSERT_TRUE(created.has_value());
    // NewGame answers with Guid.ToByteArray(): sixteen raw bytes, not a serialized message.
    EXPECT_EQ(System::Convert::FromBase64String(*created).size(), 16u);

    const auto available = channel.Invoke("GetAvailableGames", [&](XmlWriter& writer) {
        writer.WriteElementString("sessionID", sessionID);
    });
    ASSERT_TRUE(available.has_value());

    const auto bytes = System::Convert::FromBase64String(*available);
    const std::string document(bytes.begin(), bytes.end());

    // The service's payload wraps Message in a second Message element -- the behaviour the
    // ported data model reproduces rather than corrects.
    EXPECT_NE(document.find("<Message><Message ContentType=\"AvailableGames\""), std::string::npos);
    EXPECT_NE(document.find("Name=\"" + gameName + "\""), std::string::npos);
}
