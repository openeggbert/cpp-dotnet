// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
//
// The envelopes here are not invented. They were captured on 2026-09-08 from SAMPLE-071's
// unchanged original WCF service running under mono -- the request is one the service actually
// accepted, and the replies are bytes it actually sent. Encoding is only interesting if it
// matches a real service, so the expectations are that service's own output.
#include <gtest/gtest.h>

#include <string>

#include "System/ServiceModel/Channels/SoapMessageEncoder.hpp"
#include "System/ServiceModel/CommunicationException.hpp"
#include "System/ServiceModel/FaultException.hpp"
#include "System/Xml/XmlWriter.hpp"

using System::ServiceModel::CommunicationException;
using System::ServiceModel::FaultException;
using System::ServiceModel::Channels::SoapMessageEncoder;
using System::Xml::XmlWriter;

namespace {

constexpr const char* kTempuri = "http://tempuri.org/";

} // namespace

TEST(SoapMessageEncoderTest, WritesTheRequestTheOriginalServiceAccepted)
{
    const std::string envelope =
        SoapMessageEncoder::WriteRequest("Register", kTempuri, [](XmlWriter& writer) {
            writer.WriteElementString("clientURI", "http://127.0.0.1:9999/push/");
            writer.WriteElementString("name", "Kolatt");
            writer.WriteElementString("playerID", "-1");
        });

    EXPECT_EQ(envelope,
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
              "<Register xmlns=\"http://tempuri.org/\">"
              "<clientURI>http://127.0.0.1:9999/push/</clientURI>"
              "<name>Kolatt</name>"
              "<playerID>-1</playerID>"
              "</Register>"
              "</s:Body></s:Envelope>");
}

TEST(SoapMessageEncoderTest, WritesTheOperationElementEvenWithNoParameters)
{
    // A parameterless operation still needs its wrapping element written out in full: a
    // collapsed <Op /> and an explicit <Op></Op> are the same document to a parser, and this
    // keeps the form the service's own replies use.
    const std::string envelope = SoapMessageEncoder::WriteRequest("Ping", kTempuri, nullptr);

    EXPECT_NE(envelope.find("<Ping xmlns=\"http://tempuri.org/\"></Ping>"), std::string::npos);
}

TEST(SoapMessageEncoderTest, EscapesWhatAPlayerMightTypeIntoAName)
{
    const std::string envelope =
        SoapMessageEncoder::WriteRequest("NewGame", kTempuri, [](XmlWriter& writer) {
            writer.WriteElementString("name", "Bob & <Alice>");
        });

    EXPECT_NE(envelope.find("<name>Bob &amp; &lt;Alice&gt;</name>"), std::string::npos);
}

TEST(SoapMessageEncoderTest, BuildsTheActionHeaderTheServiceRoutesOn)
{
    EXPECT_EQ(SoapMessageEncoder::ActionFor(kTempuri, "IYachtService", "Register"),
              "http://tempuri.org/IYachtService/Register");
    // The namespace need not end in a slash for the action to be well formed.
    EXPECT_EQ(SoapMessageEncoder::ActionFor("http://tempuri.org", "IYachtService", "Register"),
              "http://tempuri.org/IYachtService/Register");
}

TEST(SoapMessageEncoderTest, ReadsTheResultOutOfTheServicesOwnReply)
{
    const std::string reply =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<RegisterResponse xmlns=\"http://tempuri.org/\"><RegisterResult>100</RegisterResult>"
        "</RegisterResponse></s:Body></s:Envelope>";

    const auto result = SoapMessageEncoder::ReadResult(reply, "Register");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "100");
}

TEST(SoapMessageEncoderTest, ReadsABase64ResultWhole)
{
    // GetAvailableGames' real reply, whose result is a Base64 Message rather than a number.
    const std::string reply =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<GetAvailableGamesResponse xmlns=\"http://tempuri.org/\">"
        "<GetAvailableGamesResult>PE1lc3NhZ2U+</GetAvailableGamesResult>"
        "</GetAvailableGamesResponse></s:Body></s:Envelope>";

    const auto result = SoapMessageEncoder::ReadResult(reply, "GetAvailableGames");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "PE1lc3NhZ2U+");
}

TEST(SoapMessageEncoderTest, AVoidOperationsReplyHasNoResult)
{
    const std::string reply =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<UnregisterResponse xmlns=\"http://tempuri.org/\" /></s:Body></s:Envelope>";

    EXPECT_FALSE(SoapMessageEncoder::ReadResult(reply, "Unregister").has_value());
}

TEST(SoapMessageEncoderTest, AnEmptyResultElementIsAnEmptyStringNotAMissingResult)
{
    const std::string reply =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<GetScoreCardResponse xmlns=\"http://tempuri.org/\"><GetScoreCardResult />"
        "</GetScoreCardResponse></s:Body></s:Envelope>";

    const auto result = SoapMessageEncoder::ReadResult(reply, "GetScoreCard");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "");
}

TEST(SoapMessageEncoderTest, AFaultIsRaisedWithTheReasonTheServiceGave)
{
    const std::string fault =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<s:Fault><faultcode>s:Client</faultcode>"
        "<faultstring>Session 7 is not registered.</faultstring></s:Fault>"
        "</s:Body></s:Envelope>";

    try {
        SoapMessageEncoder::ReadResult(fault, "JoinGame");
        FAIL() << "a fault must not be reported as a result";
    } catch (const FaultException& e) {
        EXPECT_STREQ(e.what(), "Session 7 is not registered.");
        EXPECT_EQ(e.getCodeProperty(), "s:Client");
    }
}

TEST(SoapMessageEncoderTest, AnotherOperationsReplyIsNotSilentlyAccepted)
{
    const std::string reply =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<RegisterResponse xmlns=\"http://tempuri.org/\"><RegisterResult>100</RegisterResult>"
        "</RegisterResponse></s:Body></s:Envelope>";

    EXPECT_THROW((void)SoapMessageEncoder::ReadResult(reply, "JoinGame"), CommunicationException);
}

TEST(SoapMessageEncoderTest, SomethingThatIsNotAnEnvelopeIsACommunicationFailure)
{
    EXPECT_THROW((void)SoapMessageEncoder::ReadResult("<html>404</html>", "Register"),
                 CommunicationException);
    EXPECT_THROW((void)SoapMessageEncoder::ReadResult("not xml at all", "Register"),
                 CommunicationException);
}
