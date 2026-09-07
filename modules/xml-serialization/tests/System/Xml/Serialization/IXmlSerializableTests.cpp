// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
//
// The interface a type implements when it owns its own XML form rather than letting the
// serializer derive one from its members.
//
// The cases here are the asymmetry that makes this interface easy to get wrong: WriteXml writes
// the content of an element the caller has already opened, while ReadXml is handed that element
// and has to consume it, end tag included. An implementation that writes its own wrapper
// produces one element too many; one that leaves the end tag behind leaves the reader parked on
// it and the next sibling is read as a child.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "System/Xml/Serialization/IXmlSerializable.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"

using System::Xml::Serialization::IXmlSerializable;
using System::Xml::XmlNodeType;
using System::Xml::XmlReader;
using System::Xml::XmlWriter;

namespace {

// A type whose XML form it decides itself: two scores written as elements, with the count of
// them carried in an attribute the way a hand-written format usually does it.
class ScoreCard final : public IXmlSerializable {
public:
    std::vector<int> Scores;

    void WriteXml(XmlWriter& writer) const override
    {
        writer.WriteAttributeString("count", std::to_string(Scores.size()));
        for (int score : Scores) {
            writer.WriteElementString("Score", std::to_string(score));
        }
    }

    void ReadXml(XmlReader& reader) override
    {
        Scores.clear();
        // Step past the wrapping start element, then take every Score until its end tag.
        reader.Read();
        while (reader.getNodeTypeProperty() != XmlNodeType::EndElement &&
               reader.getNodeTypeProperty() != XmlNodeType::None) {
            if (reader.getNodeTypeProperty() == XmlNodeType::Element &&
                reader.getNameProperty() == "Score") {
                reader.Read();
                Scores.push_back(std::stoi(reader.getValueProperty()));
                reader.Read();   // the Score end tag
            }
            reader.Read();
        }
        reader.Read();   // consume the wrapping end tag, as the contract requires
    }
};

std::string WriteWrapped(const IXmlSerializable& value, const std::string& elementName)
{
    std::unique_ptr<XmlWriter> writer(XmlWriter::CreateToString());
    writer->WriteStartElement(elementName);
    value.WriteXml(*writer);
    writer->WriteEndElement();
    return writer->ToString();
}

} // namespace

TEST(IXmlSerializableTests, WriteXmlWritesContentIntoTheCallersElement)
{
    ScoreCard card;
    card.Scores = {30, 0, 25};

    const std::string xml = WriteWrapped(card, "ScoreCard");

    // One wrapping element, written by the caller -- not by WriteXml.
    EXPECT_NE(xml.find("<ScoreCard"), std::string::npos);
    EXPECT_EQ(xml.find("<ScoreCard", xml.find("<ScoreCard") + 1), std::string::npos);
    EXPECT_NE(xml.find("count=\"3\""), std::string::npos);
    EXPECT_NE(xml.find("<Score>30</Score>"), std::string::npos);
    EXPECT_NE(xml.find("<Score>0</Score>"), std::string::npos);
    EXPECT_NE(xml.find("<Score>25</Score>"), std::string::npos);
}

TEST(IXmlSerializableTests, ReadXmlRestoresWhatWriteXmlProduced)
{
    ScoreCard written;
    written.Scores = {30, 0, 25, 12};

    std::unique_ptr<XmlReader> reader(
        XmlReader::CreateFromString(WriteWrapped(written, "ScoreCard")));
    reader->MoveToContent();

    ScoreCard read;
    read.ReadXml(*reader);

    EXPECT_EQ(read.Scores, written.Scores);
}

TEST(IXmlSerializableTests, ReadXmlConsumesItsOwnEndTagSoASiblingIsStillReachable)
{
    // Two cards side by side is what proves the end tag was consumed: if ReadXml left the
    // reader on the first card's end tag, the second card would never be found.
    std::unique_ptr<XmlWriter> writer(XmlWriter::CreateToString());
    writer->WriteStartElement("Game");
    for (const auto& scores : {std::vector<int>{1, 2}, std::vector<int>{7}}) {
        ScoreCard card;
        card.Scores = scores;
        writer->WriteStartElement("ScoreCard");
        card.WriteXml(*writer);
        writer->WriteEndElement();
    }
    writer->WriteEndElement();

    std::unique_ptr<XmlReader> reader(XmlReader::CreateFromString(writer->ToString()));
    reader->MoveToContent();
    reader->Read();          // into the first ScoreCard

    ScoreCard first;
    first.ReadXml(*reader);
    ScoreCard second;
    second.ReadXml(*reader);

    EXPECT_EQ(first.Scores, (std::vector<int>{1, 2}));
    EXPECT_EQ(second.Scores, (std::vector<int>{7}));
}

TEST(IXmlSerializableTests, GetSchemaReturnsNullAsDotNetInstructs)
{
    ScoreCard card;
    EXPECT_EQ(card.GetSchema(), nullptr);
}
