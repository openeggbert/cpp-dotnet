// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
//
// Base64 element content, and the explicit end tag that goes with it.
//
// Both come from one real format: SAMPLE-071's PlayerInformation writes a twelve-byte score
// card as Base64 content and closes its element with WriteFullEndElement, and its reader walks
// the document by counting nodes. A collapsed `<PlayerInformation />` and a
// `<PlayerInformation></PlayerInformation>` are the same document to a parser and two different
// documents to that reader.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"

using SharpRuntime::bytecs;
using System::Xml::XmlReader;
using System::Xml::XmlWriter;

namespace {

std::string WriteScoreCard(const std::vector<bytecs>& scores)
{
    std::unique_ptr<XmlWriter> writer(XmlWriter::CreateToString());
    writer->WriteStartElement("PlayerInformation");
    writer->WriteAttributeString("Name", "Kolatt");
    writer->WriteBase64(scores, 0, static_cast<SharpRuntime::intcs>(scores.size()));
    writer->WriteFullEndElement();
    return writer->ToString();
}

} // namespace

TEST(XmlBase64Test, WriteBase64EncodesTheRequestedRangeOnly)
{
    const std::vector<bytecs> bytes{0, 1, 2, 3, 4, 5};
    std::unique_ptr<XmlWriter> writer(XmlWriter::CreateToString());
    writer->WriteStartElement("Chunk");
    writer->WriteBase64(bytes, 2, 3);   // {2, 3, 4}
    writer->WriteFullEndElement();

    // AgME is base64 of 02 03 04.
    EXPECT_NE(writer->ToString().find("<Chunk>AgME</Chunk>"), std::string::npos);
}

TEST(XmlBase64Test, WriteFullEndElementKeepsTheEndTagAnEmptyElementWouldCollapse)
{
    std::unique_ptr<XmlWriter> full(XmlWriter::CreateToString());
    full->WriteStartElement("Empty");
    full->WriteFullEndElement();

    std::unique_ptr<XmlWriter> collapsed(XmlWriter::CreateToString());
    collapsed->WriteStartElement("Empty");
    collapsed->WriteEndElement();

    EXPECT_NE(full->ToString().find("<Empty></Empty>"), std::string::npos);
    EXPECT_EQ(collapsed->ToString().find("<Empty></Empty>"), std::string::npos);
}

TEST(XmlBase64Test, WriteFullEndElementWithoutAnOpenElementThrows)
{
    std::unique_ptr<XmlWriter> writer(XmlWriter::CreateToString());
    EXPECT_THROW(writer->WriteFullEndElement(), System::InvalidOperationException);
}

TEST(XmlBase64Test, WriteBase64RejectsARangePastTheEndOfTheBuffer)
{
    const std::vector<bytecs> bytes{1, 2, 3};
    std::unique_ptr<XmlWriter> writer(XmlWriter::CreateToString());
    writer->WriteStartElement("Chunk");

    EXPECT_THROW(writer->WriteBase64(bytes, 1, 5), System::ArgumentException);
    EXPECT_THROW(writer->WriteBase64(bytes, -1, 1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(writer->WriteBase64(bytes, 0, -1), System::ArgumentOutOfRangeException);
}

TEST(XmlBase64Test, ReadContentAsBase64ReadsBackWhatWriteBase64Wrote)
{
    const std::vector<bytecs> scores{0, 6, 12, 0, 20, 5, 25, 30, 0, 40, 0, 50};
    std::unique_ptr<XmlReader> reader(XmlReader::CreateFromString(WriteScoreCard(scores)));

    reader->MoveToContent();
    reader->Read();   // onto the Base64 text content

    std::vector<bytecs> read(12, 0);
    EXPECT_EQ(reader->ReadContentAsBase64(read, 0, 12), 12);
    EXPECT_EQ(read, scores);
}

TEST(XmlBase64Test, ReadContentAsBase64ContinuesWhereTheLastCallStopped)
{
    const std::vector<bytecs> scores{1, 2, 3, 4, 5, 6};
    std::unique_ptr<XmlReader> reader(XmlReader::CreateFromString(WriteScoreCard(scores)));
    reader->MoveToContent();
    reader->Read();

    // Room for eight, so index + count stays inside the buffer on every call -- .NET
    // validates that range the same way, and a shorter buffer would throw rather than
    // return a short count.
    std::vector<bytecs> read(8, 0);
    EXPECT_EQ(reader->ReadContentAsBase64(read, 0, 4), 4);
    EXPECT_EQ(reader->ReadContentAsBase64(read, 4, 4), 2);   // only two are left
    EXPECT_EQ(reader->ReadContentAsBase64(read, 4, 4), 0);   // and then none
    EXPECT_EQ(std::vector<bytecs>(read.begin(), read.begin() + 6), scores);
}

TEST(XmlBase64Test, ReadContentAsBase64RejectsARangePastTheEndOfTheBuffer)
{
    std::unique_ptr<XmlReader> reader(
        XmlReader::CreateFromString(WriteScoreCard(std::vector<bytecs>{1, 2, 3})));
    reader->MoveToContent();
    reader->Read();

    std::vector<bytecs> read(3, 0);
    EXPECT_THROW(reader->ReadContentAsBase64(read, 1, 5), System::ArgumentException);
    EXPECT_THROW(reader->ReadContentAsBase64(read, -1, 1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(reader->ReadContentAsBase64(read, 0, -1), System::ArgumentOutOfRangeException);
}
