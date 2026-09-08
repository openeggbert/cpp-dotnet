// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
//
// The host and the channel against each other: this runtime's client calling this runtime's
// service, over the same wire a real basic-profile service speaks.
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "System/ServiceModel/BasicHttpBinding.hpp"
#include "System/ServiceModel/Channels/SoapChannel.hpp"
#include "System/ServiceModel/EndpointAddress.hpp"
#include "System/ServiceModel/FaultException.hpp"
#include "System/Net/Http/HttpClient.hpp"
#include "System/Net/Http/HttpContent.hpp"
#include "System/Net/Http/HttpResponseMessage.hpp"
#include "System/ServiceModel/ServiceHost.hpp"
#include "System/Xml/XmlNodeType.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"

using System::ServiceModel::BasicHttpBinding;
using System::ServiceModel::EndpointAddress;
using System::ServiceModel::FaultException;
using System::ServiceModel::ServiceHost;
using System::ServiceModel::Channels::SoapChannel;
using System::Xml::XmlReader;
using System::Xml::XmlWriter;

namespace {

constexpr const char* kNamespace = "http://tempuri.org/";
constexpr const char* kAddress = "http://localhost:18999/TestService/";

// Reads one named parameter out of the operation element the host handed over.
std::string ReadParameter(XmlReader& reader, const std::string& name)
{
    while (reader.Read()) {
        if (reader.getNodeTypeProperty() == System::Xml::XmlNodeType::Element &&
            reader.getNameProperty() == name) {
            if (!reader.Read()) {
                return {};
            }
            return reader.getValueProperty();
        }
    }
    return {};
}

class HostedService {
public:
    HostedService()
        : host_(EndpointAddress(kAddress), kNamespace, "ITestService")
    {
        host_.AddOperation("Add", [](XmlReader& parameters) -> std::optional<std::string> {
            const int left = std::stoi(ReadParameter(parameters, "left"));
            const int right = std::stoi(ReadParameter(parameters, "right"));
            return std::to_string(left + right);
        });
        host_.AddOperation("Greet", [](XmlReader& parameters) -> std::optional<std::string> {
            return "Hello, " + ReadParameter(parameters, "name");
        });
        host_.AddOperation("Ping", [](XmlReader&) -> std::optional<std::string> {
            return std::nullopt;
        });
        host_.AddOperation("Explode", [](XmlReader&) -> std::optional<std::string> {
            throw std::runtime_error("the service refused");
        });
        host_.SetMetadata("<wsdl:definitions />", {{"xsd0", "<xs:schema />"}});
        host_.Open();
    }

    ~HostedService() { host_.Close(); }

private:
    ServiceHost host_;
};

SoapChannel MakeChannel()
{
    return SoapChannel(BasicHttpBinding(), EndpointAddress(kAddress), kNamespace, "ITestService");
}

} // namespace

TEST(ServiceHostTest, AnswersAnOperationThisRuntimesClientCalls)
{
    HostedService service;
    auto channel = MakeChannel();

    const auto sum = channel.Invoke("Add", [](XmlWriter& writer) {
        writer.WriteElementString("left", "17");
        writer.WriteElementString("right", "25");
    });

    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(*sum, "42");
}

TEST(ServiceHostTest, CarriesTextBothWaysWithoutMangling)
{
    HostedService service;
    auto channel = MakeChannel();

    const auto greeting = channel.Invoke("Greet", [](XmlWriter& writer) {
        writer.WriteElementString("name", "Bob & <Alice>");
    });

    ASSERT_TRUE(greeting.has_value());
    EXPECT_EQ(*greeting, "Hello, Bob & <Alice>");
}

TEST(ServiceHostTest, AVoidOperationAnswersWithNoResult)
{
    HostedService service;
    auto channel = MakeChannel();

    EXPECT_FALSE(channel.Invoke("Ping", nullptr).has_value());
}

TEST(ServiceHostTest, AnOperationThatThrowsBecomesAFaultWithItsReason)
{
    HostedService service;
    auto channel = MakeChannel();

    try {
        (void)channel.Invoke("Explode", nullptr);
        FAIL() << "a throwing operation must reach the client as a fault";
    } catch (const FaultException& e) {
        EXPECT_STREQ(e.what(), "the service refused");
        EXPECT_EQ(e.getCodeProperty(), "s:Server");
    }
}

TEST(ServiceHostTest, AnUnknownOperationIsRefusedRatherThanIgnored)
{
    HostedService service;
    auto channel = MakeChannel();

    EXPECT_THROW((void)channel.Invoke("NoSuchOperation", nullptr), FaultException);
}

TEST(ServiceHostTest, PublishesTheMetadataTheServiceGaveIt)
{
    HostedService service;

    // Fetched the way a client discovers a contract: a plain GET on the endpoint.
    System::Net::Http::HttpClient client;
    const auto wsdl = client.Get(std::string(kAddress) + "?wsdl");
    ASSERT_NE(wsdl, nullptr);
    ASSERT_NE(wsdl->getContentProperty(), nullptr);
    EXPECT_NE(wsdl->getContentProperty()->ReadAsString().find("wsdl:definitions"),
              std::string::npos);

    const auto schema = client.Get(std::string(kAddress) + "?xsd=xsd0");
    ASSERT_NE(schema, nullptr);
    ASSERT_NE(schema->getContentProperty(), nullptr);
    EXPECT_NE(schema->getContentProperty()->ReadAsString().find("xs:schema"), std::string::npos);
}
