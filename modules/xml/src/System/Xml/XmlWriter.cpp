// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#include "System/Xml/XmlWriter.hpp"
#include <tinyxml2/tinyxml2.h>
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Convert.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Xml/XmlConvert.hpp"
#include "System/Xml/XmlException.hpp"
#include "System/Xml/detail/XmlLexicalSanitizer.hpp"
#include <cstdio>
#include <stack>

namespace System::Xml {

// ---------------------------------------------------------------------------
// Opaque state
// ---------------------------------------------------------------------------

struct XmlWriterState {
    tinyxml2::XMLDocument          doc;
    std::stack<tinyxml2::XMLNode*> nodeStack;  // top = current parent
    std::string                    filePath;
    bool                           hasDeclaration = false;
    bool                           closed         = false;
    XmlWriterSettings              settings;
};

// ---------------------------------------------------------------------------
// Name and lifecycle validation (ticket #2076, SR-AUD-349)
//
// Every writer door that takes an XML *name* routes through XmlConvert::VerifyName --
// the validator this module already ships and that XmlDocument::CreateElement already
// uses. Before this, the writer forwarded names straight to tinyxml2, so a successful
// write could produce markup this module's OWN XmlReader then rejected: measured,
// WriteStartElement("1bad") emitted "<1bad/>" and CreateFromString on that text threw
// XML_ERROR_PARSING, while XmlDocument::CreateElement("1bad") already threw XmlException
// for the same name in the same program.
//
// VerifyName throws XmlException("Invalid XML name: '...'.") for a malformed name and
// System::ArgumentException for an empty one; both are the validator's own pre-existing
// choices and are deliberately not re-mapped here, so the writer door and the DOM door
// report an identical diagnostic for identical input.
// ---------------------------------------------------------------------------

/// Rejects any write once Close() has been called. Close() itself, Flush() and ToString()
/// stay usable -- ToString() is how an in-memory writer's result is read back, and the
/// destructor calls Close() unconditionally.
/// Rejects content that would be silently truncated at the tinyxml2 `const char*` boundary
/// (ticket #2085). Measured, SIX public doors lost every byte after an embedded NUL and gave
/// the caller no diagnostic; the finding named three. See detail::ContainsNul for why NUL is
/// rejected unconditionally while the other non-Char characters are a separate decision.
static void ThrowIfContainsNul(const std::string& text, const char* member, const char* what) {
    if (detail::ContainsNul(text))
        throw XmlException(std::string("XmlWriter::") + member + ": the " + what +
                           " contains a NUL character, which cannot be represented in XML.");

    // Ticket #2349. The other characters outside the XML 1.0 Char production were emitted RAW,
    // so the emitted document was not well-formed XML. .NET rejects them when
    // XmlWriterSettings.CheckCharacters is set, which it is by default
    // (XmlWriterSettings.cs:513; XmlEncodedRawTextWriter.cs:1630-1654). The exception TYPE is
    // .NET's ArgumentException rather than this door's XmlException, because that is what
    // XmlConvert.CreateInvalidCharException produces (XmlConvert.cs:1614-1622) -- the NUL case
    // above keeps XmlException because it is this port's own truncation guard (#2085), not a
    // transcription of a .NET check.
    const std::size_t bad = detail::FindNonCharCodePoint(text);
    if (bad != std::string::npos) {
        std::uint32_t cp = 0;
        std::size_t   len = 0;
        if (!System::detail::TryDecodeUtf8Scalar(text, bad, cp, len)) cp = static_cast<unsigned char>(text[bad]);
        throw System::ArgumentException(detail::InvalidCharacterMessage(cp));
    }
}

static void ThrowIfClosed(const XmlWriterState* state, const char* member) {
    if (state && state->closed)
        throw System::InvalidOperationException(std::string("XmlWriter::") + member +
                                                ": the writer is closed.");
}

/// Rejects a write that has no open element to attach to. This is the "invalid call
/// ordering" clause of SR-AUD-349: an unbalanced WriteEndElement() and an attribute
/// written with no element open were both silently discarded.
static void ThrowIfNoOpenElement(const XmlWriterState* state, const char* member) {
    if (!state || state->nodeStack.size() <= 1 || !state->nodeStack.top()->ToElement())
        throw System::InvalidOperationException(std::string("XmlWriter::") + member +
                                                ": there is no open element.");
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Well-formedness self-healing. The three transforms moved to
// System/Xml/detail/XmlLexicalSanitizer.hpp by ticket #2196 (SR-AUD-335): they were
// file-local here, so this writer self-healed while System::Xml::Linq's direct
// SerializeTo serializers -- the ones behind ToString() and Save(fileName) -- emitted the
// raw delimiters. One shared definition is what makes "both doors emit the same text" a
// property rather than a coincidence. The behaviour is unchanged, character for character.
// ---------------------------------------------------------------------------

using System::Xml::detail::SanitizeCDataText;
using System::Xml::detail::SanitizeCommentText;
using System::Xml::detail::SanitizeProcessingInstructionText;

XmlWriter::XmlWriter(std::unique_ptr<XmlWriterState> s) : state_(std::move(s)) {
    // Start with the document as the root parent
    state_->nodeStack.push(&state_->doc);
}

// Best-effort, non-throwing (audit finding A-02, 2026-07-14) -- see DeflateStream::~DeflateStream's
// identical doc-comment for the full rationale and confirmed std::terminate repro. Close() can
// throw via Flush() when tinyxml2's SaveFile() fails (e.g. an unwritable path or a full disk).
XmlWriter::~XmlWriter() { try { Close(); } catch (...) {} }

// ---------------------------------------------------------------------------
// Write methods
// ---------------------------------------------------------------------------

void XmlWriter::WriteStartDocument() {
    ThrowIfClosed(state_.get(), "WriteStartDocument");
    if (!state_ || state_->hasDeclaration) return;
    state_->doc.InsertFirstChild(state_->doc.NewDeclaration());
    state_->hasDeclaration = true;
}

void XmlWriter::WriteEndDocument() {
    ThrowIfClosed(state_.get(), "WriteEndDocument");
    Flush();
}

void XmlWriter::WriteStartElement(const std::string& localName) {
    ThrowIfClosed(state_.get(), "WriteStartElement");
    (void)XmlConvert::VerifyName(localName);
    if (!state_ || state_->nodeStack.empty()) return;
    tinyxml2::XMLElement* el = state_->doc.NewElement(localName.c_str());
    state_->nodeStack.top()->InsertEndChild(el);
    state_->nodeStack.push(el);
}

void XmlWriter::WriteEndElement() {
    ThrowIfClosed(state_.get(), "WriteEndElement");
    // An unbalanced WriteEndElement() used to be silently discarded, so a caller that
    // popped one level too many produced a document whose nesting was not the one it wrote.
    if (!state_ || state_->nodeStack.size() <= 1) // never pop the document
        throw System::InvalidOperationException("XmlWriter::WriteEndElement: there is no open element.");
    state_->nodeStack.pop();
}

void XmlWriter::WriteFullEndElement() {
    ThrowIfClosed(state_.get(), "WriteFullEndElement");
    if (!state_ || state_->nodeStack.size() <= 1) // never pop the document
        throw System::InvalidOperationException("XmlWriter::WriteFullEndElement: there is no open element.");
    // tinyxml2 collapses a childless element to `<name />` when it prints. An empty text
    // child is what makes it print the separate end tag instead, which is the whole
    // difference between this and WriteEndElement().
    tinyxml2::XMLNode* node = state_->nodeStack.top();
    if (node->NoChildren()) {
        node->InsertEndChild(state_->doc.NewText(""));
    }
    state_->nodeStack.pop();
}

void XmlWriter::WriteBase64(const std::vector<SharpRuntime::bytecs>& buffer,
                            SharpRuntime::intcs index, SharpRuntime::intcs count) {
    ThrowIfClosed(state_.get(), "WriteBase64");
    if (index < 0)
        throw System::ArgumentOutOfRangeException("index");
    if (count < 0)
        throw System::ArgumentOutOfRangeException("count");
    if (static_cast<std::size_t>(index) + static_cast<std::size_t>(count) > buffer.size())
        throw System::ArgumentException("XmlWriter::WriteBase64: the range runs past the end of the buffer.");
    const std::vector<SharpRuntime::bytecs> slice(buffer.begin() + index,
                                                  buffer.begin() + index + count);
    WriteString(System::Convert::ToBase64String(slice));
}

void XmlWriter::WriteBinHex(const std::vector<SharpRuntime::bytecs>& buffer,
                            SharpRuntime::intcs index, SharpRuntime::intcs count) {
    ThrowIfClosed(state_.get(), "WriteBinHex");
    if (index < 0)
        throw System::ArgumentOutOfRangeException("index");
    if (count < 0)
        throw System::ArgumentOutOfRangeException("count");
    if (static_cast<std::size_t>(index) + static_cast<std::size_t>(count) > buffer.size())
        throw System::ArgumentException("XmlWriter::WriteBinHex: the range runs past the end of the buffer.");
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(static_cast<std::size_t>(count) * 2);
    for (SharpRuntime::intcs i = 0; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(buffer[static_cast<std::size_t>(index + i)]);
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0x0F]);
    }
    WriteString(hex);
}

void XmlWriter::WriteAttributeString(const std::string& name, const std::string& value) {
    ThrowIfClosed(state_.get(), "WriteAttributeString");
    (void)XmlConvert::VerifyName(name);
    ThrowIfContainsNul(value, "WriteAttributeString", "attribute value");
    ThrowIfNoOpenElement(state_.get(), "WriteAttributeString");
    state_->nodeStack.top()->ToElement()->SetAttribute(name.c_str(), value.c_str());
}

void XmlWriter::WriteString(const std::string& text) {
    ThrowIfClosed(state_.get(), "WriteString");
    ThrowIfContainsNul(text, "WriteString", "text");
    if (!state_ || state_->nodeStack.empty()) return;
    tinyxml2::XMLText* tn = state_->doc.NewText(text.c_str());
    state_->nodeStack.top()->InsertEndChild(tn);
}

void XmlWriter::WriteWhitespace(const std::string& whitespace) {
    ThrowIfClosed(state_.get(), "WriteWhitespace");
    if (whitespace.find_first_not_of(" \t\r\n") != std::string::npos) {
        throw System::ArgumentException("WriteWhitespace accepts only XML whitespace characters.");
    }
    WriteString(whitespace);
}

void XmlWriter::WriteElementString(const std::string& name, const std::string& value) {
    WriteStartElement(name);
    WriteString(value);
    WriteEndElement();
}

void XmlWriter::WriteComment(const std::string& text) {
    ThrowIfClosed(state_.get(), "WriteComment");
    ThrowIfContainsNul(text, "WriteComment", "comment text");
    if (!state_ || state_->nodeStack.empty()) return;
    tinyxml2::XMLComment* cmt = state_->doc.NewComment(SanitizeCommentText(text).c_str());
    state_->nodeStack.top()->InsertEndChild(cmt);
}

void XmlWriter::WriteCData(const std::string& text) {
    ThrowIfClosed(state_.get(), "WriteCData");
    ThrowIfContainsNul(text, "WriteCData", "CDATA section text");
    if (!state_ || state_->nodeStack.empty()) return;
    tinyxml2::XMLText* tn = state_->doc.NewText(SanitizeCDataText(text).c_str());
    tn->SetCData(true);
    state_->nodeStack.top()->InsertEndChild(tn);
}

void XmlWriter::WriteProcessingInstruction(const std::string& target, const std::string& data) {
    ThrowIfClosed(state_.get(), "WriteProcessingInstruction");
    // The PI target is an XML name, and an unvalidated one is not merely ugly: measured,
    // WriteProcessingInstruction("a?>b", "d") emitted "<?a?>b d?>", whose "?>" closed the
    // instruction early and spilled the rest into document-level text. sanitizeProcessing-
    // InstructionText already protects the DATA; the target had no such door.
    (void)XmlConvert::VerifyName(target);
    ThrowIfContainsNul(data, "WriteProcessingInstruction", "processing-instruction data");
    if (!state_ || state_->nodeStack.empty()) return;
    std::string sanitizedData = SanitizeProcessingInstructionText(data);
    std::string text = sanitizedData.empty() ? target : (target + " " + sanitizedData);
    tinyxml2::XMLDeclaration* pi = state_->doc.NewDeclaration(text.c_str());
    state_->nodeStack.top()->InsertEndChild(pi);
}

void XmlWriter::WriteDocType(const std::string& name, const std::string& publicId,
                              const std::string& systemId, const std::string& internalSubset) {
    ThrowIfClosed(state_.get(), "WriteDocType");
    (void)XmlConvert::VerifyName(name); // the DOCTYPE root-element name is an XML name
    // Ticket #2084: the two ExternalID literals are validated with the validators this
    // module already ships, exactly as #2076 routed the four NAME doors through VerifyName.
    // VerifyPublicId rejects '"' outright (it is not a PubidChar), so a PubidLiteral is
    // always representable with the '"' this writer emits; a SystemLiteral may legally
    // contain '"', so it picks its delimiter instead. Both run BEFORE the nodeStack check,
    // so an invalid literal is reported whether or not a document is open -- the same
    // ordering VerifyName above already has.
    (void)XmlConvert::VerifyPublicId(publicId);
    (void)XmlConvert::VerifyXmlChars(systemId);
    // The last NUL vector at this door: #2084 closed publicId/systemId, the subset stayed open.
    ThrowIfContainsNul(internalSubset, "WriteDocType", "internal subset");
    if (detail::ExternalIdLiteralTerminatesDeclaration(systemId))
        throw XmlException("XmlWriter::WriteDocType: the system identifier contains '>', "
                           "which would terminate the DOCTYPE declaration: '" + systemId + "'.");
    // #2348, the subset half of the same rule. See detail::InternalSubsetTerminatesDeclaration
    // for why this is `]` followed by `>` rather than any `>`, and for the measurement that .NET
    // emits the injection too.
    if (detail::InternalSubsetTerminatesDeclaration(internalSubset))
        throw XmlException("XmlWriter::WriteDocType: the internal subset closes the DOCTYPE "
                           "declaration early with ']>', which would inject the remainder as "
                           "document markup: '" + internalSubset + "'.");
    const char systemQuote = detail::SelectExternalIdDelimiter(systemId);
    if (systemQuote == '\0')
        throw XmlException("XmlWriter::WriteDocType: the system identifier contains both a "
                           "double quote and an apostrophe and cannot be represented in a "
                           "DOCTYPE system literal: '" + systemId + "'.");
    if (!state_ || state_->nodeStack.empty()) return;
    std::string text = "DOCTYPE " + name;
    if (!publicId.empty()) {
        text += " PUBLIC \"" + publicId + "\" " + systemQuote + systemId + systemQuote;
    } else if (!systemId.empty()) {
        text += " SYSTEM ";
        text += systemQuote + systemId + systemQuote;
    }
    if (!internalSubset.empty()) text += " [" + internalSubset + "]";
    tinyxml2::XMLUnknown* dt = state_->doc.NewUnknown(text.c_str());
    state_->nodeStack.top()->InsertEndChild(dt);
}

// ---------------------------------------------------------------------------
// Text form. The document is kept as a tinyxml2 DOM while it is being written, but it is
// NOT printed with tinyxml2's XMLPrinter: measured, that printer indents with a fixed four
// spaces whatever XmlWriterSettings::IndentChars says, spells an empty element "<a/>" where
// .NET writes "<a />", writes the declaration's encoding as "UTF-8" where .NET writes
// "utf-8", ends the document with a newline .NET does not write, and keeps indenting the
// children of an element after text has been written into it, which .NET stops doing the
// moment text is written (XmlEncodedRawTextWriterIndent: WriteString sets mixedContent, and
// WriteStartElement / WriteEndElement indent only while it is clear, restoring the parent's
// flag on the way out). The emitter below reproduces those rules from XmlWriterSettings alone,
// so a document written by this class matches what .NET's XmlWriter.Create(…, settings)
// emits for the same sequence of calls.
// ---------------------------------------------------------------------------
namespace {

void AppendAttributeValue(std::string& out, const char* value) {
    for (const char* p = value ? value : ""; *p; ++p) {
        switch (*p) {
        case '&':  out += "&amp;"; break;
        case '<':  out += "&lt;"; break;
        case '>':  out += "&gt;"; break;
        case '"':  out += "&quot;"; break;
        case '\r': out += "&#xD;"; break;
        case '\n': out += "&#xA;"; break;
        case '\t': out += "&#x9;"; break;
        default:   out += *p; break;
        }
    }
}

void AppendText(std::string& out, const char* value, const XmlWriterSettings& settings) {
    for (const char* p = value ? value : ""; *p; ++p) {
        switch (*p) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '\r':
            if (settings.NewLineHandling == NewLineHandling::Replace) {
                out += settings.NewLineChars;
                if (p[1] == '\n') ++p; // "\r\n" is one line break
            } else if (settings.NewLineHandling == NewLineHandling::Entitize) {
                out += "&#xD;";
            } else {
                out += '\r';
            }
            break;
        case '\n':
            if (settings.NewLineHandling == NewLineHandling::Replace) out += settings.NewLineChars;
            else out += '\n';
            break;
        default: out += *p; break;
        }
    }
}

struct DotNetPrinter {
    explicit DotNetPrinter(const XmlWriterSettings& s) : settings(s) {}

    const XmlWriterSettings& settings;
    std::string out;
    int level = 0;
    bool wroteAnything = false;

    void Indent() {
        if (!settings.Indent || !wroteAnything) return;
        out += settings.NewLineChars;
        for (int i = 0; i < level; ++i) out += settings.IndentChars;
    }

    void Node(const tinyxml2::XMLNode* node, bool& mixed) {
        if (auto* el = node->ToElement()) {
            if (!mixed) Indent();
            out += '<';
            out += el->Name() ? el->Name() : "";
            for (const tinyxml2::XMLAttribute* a = el->FirstAttribute(); a; a = a->Next()) {
                out += ' ';
                out += a->Name() ? a->Name() : "";
                out += "=\"";
                AppendAttributeValue(out, a->Value());
                out += '"';
            }
            wroteAnything = true;
            if (!el->FirstChild()) { out += " />"; return; }
            out += '>';
            ++level;
            bool childMixed = false;
            for (const tinyxml2::XMLNode* c = el->FirstChild(); c; c = c->NextSibling())
                Node(c, childMixed);
            --level;
            if (!childMixed) Indent();
            out += "</";
            out += el->Name() ? el->Name() : "";
            out += '>';
            return;
        }
        if (auto* txt = node->ToText()) {
            mixed = true;
            if (txt->CData()) {
                out += "<![CDATA[";
                out += txt->Value() ? txt->Value() : "";
                out += "]]>";
            } else {
                AppendText(out, txt->Value(), settings);
            }
            wroteAnything = true;
            return;
        }
        if (auto* cmt = node->ToComment()) {
            if (!mixed) Indent();
            out += "<!--";
            out += cmt->Value() ? cmt->Value() : "";
            out += "-->";
            wroteAnything = true;
            return;
        }
        if (auto* decl = node->ToDeclaration()) {
            // Both the XML declaration and every other processing instruction parse as a
            // tinyxml2 declaration; WriteStartDocument() leaves tinyxml2's default text, which
            // is replaced by the declaration .NET writes for a UTF-8 document.
            std::string text = decl->Value() ? decl->Value() : "";
            const bool isDeclaration = text.rfind("xml ", 0) == 0 || text == "xml";
            if (isDeclaration) {
                if (settings.OmitXmlDeclaration) return;
                text = "xml version=\"1.0\" encoding=\"utf-8\"";
            } else if (!mixed) {
                Indent();
            }
            out += "<?";
            out += text;
            out += "?>";
            wroteAnything = true;
            return;
        }
        if (auto* unk = node->ToUnknown()) {
            if (!mixed) Indent();
            out += "<!";
            out += unk->Value() ? unk->Value() : "";
            out += '>';
            wroteAnything = true;
            return;
        }
    }

    std::string Print(const tinyxml2::XMLDocument& doc) {
        bool mixed = false;
        for (const tinyxml2::XMLNode* n = doc.FirstChild(); n; n = n->NextSibling())
            Node(n, mixed);
        return out;
    }
};

} // namespace

std::string XmlWriter::ToString() const {
    if (!state_) return {};
    DotNetPrinter printer(state_->settings);
    return printer.Print(state_->doc);
}

void XmlWriter::Flush() {
    if (!state_ || state_->filePath.empty()) return;
    DotNetPrinter printer(state_->settings);
    const std::string text = printer.Print(state_->doc);
    FILE* file = std::fopen(state_->filePath.c_str(), "wb");
    if (!file)
        throw XmlException("XmlWriter: failed to save file: " + state_->filePath);
    const bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    if (std::fclose(file) != 0 || !ok)
        throw XmlException("XmlWriter: failed to save file: " + state_->filePath);
}

void XmlWriter::Close() {
    if (!state_ || state_->closed) return; // idempotent: the destructor also calls Close()
    // Marked closed BEFORE the flush, so a writer whose save failed is still terminally
    // closed rather than accepting further writes into a document it could not persist.
    state_->closed = true;
    Flush();
    // clear the stack
    while (!state_->nodeStack.empty()) state_->nodeStack.pop();
}

// ---------------------------------------------------------------------------
// Factory methods
// ---------------------------------------------------------------------------

XmlWriter* XmlWriter::Create(const std::string& outputFileName, const XmlWriterSettings& settings) {
    auto st = std::make_unique<XmlWriterState>();
    st->filePath = outputFileName;
    st->settings = settings;
    return new XmlWriter(std::move(st));
}

XmlWriter* XmlWriter::CreateToString(const XmlWriterSettings& settings) {
    auto st = std::make_unique<XmlWriterState>();
    st->settings = settings;
    return new XmlWriter(std::move(st));
}

} // namespace System::Xml
