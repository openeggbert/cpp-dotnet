// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#include "System/Xml/XmlReader.hpp"
#include <algorithm>
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Convert.hpp"
#include <tinyxml2/tinyxml2.h>
#include "System/Xml/XmlException.hpp"
#include "System/Xml/XmlReaderSettings.hpp"
#include <cctype>
#include <stack>

namespace System::Xml {

// ---------------------------------------------------------------------------
// Internal event model
// ---------------------------------------------------------------------------

struct XmlEvent {
    XmlNodeType                              type         = XmlNodeType::None;
    std::string                              name;
    std::string                              value;
    bool                                     isEmptyElement = false;
    std::vector<std::pair<std::string,std::string>> attributes;
    /// The element this event belongs to: the element itself for Element/EndElement, the
    /// enclosing element for everything else (null at document level). Namespace lookups
    /// walk its parent chain; the document that owns it lives as long as the reader.
    const tinyxml2::XMLElement*              scope        = nullptr;
    int                                      depth        = 0;
    int                                      lineNumber   = 0;
};

// ---------------------------------------------------------------------------
// Opaque state
// ---------------------------------------------------------------------------

struct XmlReaderState {
    // PEDANTIC_WHITESPACE keeps whitespace-only text nodes, which .NET reports as Whitespace
    // nodes and includes in ReadElementContentAsString(): `<Tab>\t</Tab>` reads a tab, not "".
    tinyxml2::XMLDocument              doc{true, tinyxml2::PEDANTIC_WHITESPACE};
    std::vector<XmlEvent>              events;
    int                                pos       = -1;  // before first Read()
    int                                attrIndex = -1;  // attribute cursor (-1 = on element)
    ReadState                          readState = ReadState::Initial;
    // ReadContentAsBase64 may be called repeatedly to drain one text node in chunks, so how
    // far it has drained has to survive between calls. Keyed on the event it was draining;
    // moving to another node starts over.
    int                                base64Event    = -1;
    std::size_t                        base64Consumed = 0;
};

// ---------------------------------------------------------------------------
// DOM → flat event list
// ---------------------------------------------------------------------------

// tinyxml2 has no distinct "processing instruction" node kind: both the real
// `<?xml ...?>` declaration and every other `<?target data?>` PI parse as XMLDeclaration,
// with Value() holding the raw "target data" text. Split the leading whitespace-delimited
// target token from the rest so the two can be told apart (target == "xml", the only
// reserved PI target per the XML spec, means a real declaration).
static void splitTargetAndData(const std::string& raw, std::string& target, std::string& data) {
    size_t i = 0;
    while (i < raw.size() && !std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    target = raw.substr(0, i);
    while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    data = raw.substr(i);
}

static void buildEvents(tinyxml2::XMLNode* node, std::vector<XmlEvent>& out,
                        const tinyxml2::XMLElement* scope, int depth) {
    if (auto* decl = node->ToDeclaration()) {
        std::string target, data;
        splitTargetAndData(decl->Value() ? decl->Value() : "", target, data);
        XmlEvent e;
        e.scope = scope; e.depth = depth; e.lineNumber = node->GetLineNum();
        bool isRealDeclaration = target.size() == 3 &&
            (target[0] == 'x' || target[0] == 'X') &&
            (target[1] == 'm' || target[1] == 'M') &&
            (target[2] == 'l' || target[2] == 'L');
        e.type  = isRealDeclaration ? XmlNodeType::XmlDeclaration : XmlNodeType::ProcessingInstruction;
        e.name  = target;
        e.value = data;
        out.push_back(std::move(e));
        return;
    }
    if (auto* el = node->ToElement()) {
        XmlEvent e;
        e.type          = XmlNodeType::Element;
        e.name          = el->Name() ? el->Name() : "";
        e.isEmptyElement = el->ClosingType() == tinyxml2::XMLElement::CLOSED;
        e.scope = el; e.depth = depth; e.lineNumber = el->GetLineNum();
        for (const tinyxml2::XMLAttribute* a = el->FirstAttribute(); a; a = a->Next())
            e.attributes.emplace_back(a->Name() ? a->Name() : "",
                                      a->Value() ? a->Value() : "");
        const bool isEmpty = e.isEmptyElement;
        out.push_back(std::move(e));

        for (tinyxml2::XMLNode* child = el->FirstChild(); child; child = child->NextSibling())
            buildEvents(child, out, el, depth + 1);

        if (!isEmpty) {
            XmlEvent ee;
            ee.type = XmlNodeType::EndElement;
            ee.name = el->Name() ? el->Name() : "";
            ee.scope = el; ee.depth = depth;
            // tinyxml2 keeps the start tag's line only; the end tag is reported on the line
            // of the element's last child when it has one.
            ee.lineNumber = el->LastChild() ? el->LastChild()->GetLineNum() : el->GetLineNum();
            out.push_back(std::move(ee));
        }
        return;
    }
    if (auto* txt = node->ToText()) {
        XmlEvent e;
        e.value = txt->Value() ? txt->Value() : "";
        const bool whitespaceOnly = !txt->CData() &&
            e.value.find_first_not_of(" \t\r\n") == std::string::npos;
        e.type  = txt->CData() ? XmlNodeType::CDATA
                : whitespaceOnly ? XmlNodeType::Whitespace : XmlNodeType::Text;
        e.scope = scope; e.depth = depth; e.lineNumber = node->GetLineNum();
        out.push_back(std::move(e));
        return;
    }
    if (auto* cmt = node->ToComment()) {
        XmlEvent e;
        e.type  = XmlNodeType::Comment;
        e.value = cmt->Value() ? cmt->Value() : "";
        e.scope = scope; e.depth = depth; e.lineNumber = node->GetLineNum();
        out.push_back(std::move(e));
        return;
    }
    if (auto* unk = node->ToUnknown()) {
        // tinyxml2 parses every other `<!...>` form (DOCTYPE, and anything else it
        // doesn't specifically recognize) as XMLUnknown, with Value() holding the raw
        // text between "<!" and the next ">". DOCTYPE's Name is the root element name
        // per the DOM spec; anything else is reported best-effort with the raw text as
        // Name rather than silently dropped.
        std::string raw = unk->Value() ? unk->Value() : "";
        XmlEvent e;
        e.type = XmlNodeType::DocumentType;
        e.scope = scope; e.depth = depth; e.lineNumber = node->GetLineNum();
        if (raw.rfind("DOCTYPE", 0) == 0) {
            std::string keyword, rest;
            splitTargetAndData(raw, keyword, rest);
            std::string name;
            splitTargetAndData(rest, name, rest);
            e.name = name;
        } else {
            e.name = raw;
        }
        out.push_back(std::move(e));
        return;
    }
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

XmlReader::XmlReader(std::unique_ptr<XmlReaderState> s) : state_(std::move(s)) {}
XmlReader::~XmlReader() = default;

// ---------------------------------------------------------------------------
// Closed-state enforcement (ticket #2078, SR-AUD-348)
//
// Close() records ReadState::Closed and nothing else, so the state was a field rather
// than a precondition: measured, Read() had no guard AND assigned ReadState::Interactive
// on its way out, so the closed state did not survive a single call. Three members
// destroyed it -- Read(), and ReadStartElement()/ReadEndElement()/ReadElementContentAs-
// String(), which advance through Read() -- while MoveToElement(), MoveToNextAttribute()
// and GetAttribute() kept answering from a closed reader.
//
// The repair needs no new state and no new policy: this class ALREADY implements one
// terminal read state correctly. Measured on the same document, a reader driven past its
// last event reports EndOfFile, returns false from every further Read(), and never leaves
// that state. Closed is now the same shape, and every accessor reuses the class's own
// pre-existing "there is no current node" answer (None / "" / false) rather than a value
// invented for this ticket.
// ---------------------------------------------------------------------------

/// Terminally closed. Close() is the only writer of ReadState::Closed, and a null state_
/// already reports Closed from getReadStateProperty(), so both spellings agree here.
static bool isClosed(const XmlReaderState* s) {
    return !s || s->readState == ReadState::Closed;
}

/// True when the cursor is parked on a real event. A closed reader is on no node.
static bool hasCurrentNode(const XmlReaderState* s) {
    return !isClosed(s) && s->pos >= 0 && s->pos < static_cast<int>(s->events.size());
}

// ---------------------------------------------------------------------------
// Property accessors
// ---------------------------------------------------------------------------

XmlNodeType XmlReader::getNodeTypeProperty() const {
    if (!hasCurrentNode(state_.get()))
        return XmlNodeType::None;
    if (state_->attrIndex >= 0)
        return XmlNodeType::Attribute;
    return state_->events[static_cast<size_t>(state_->pos)].type;
}

std::string XmlReader::getNameProperty() const {
    if (!hasCurrentNode(state_.get()))
        return {};
    const auto& ev = state_->events[static_cast<size_t>(state_->pos)];
    if (state_->attrIndex >= 0 &&
        state_->attrIndex < static_cast<int>(ev.attributes.size()))
        return ev.attributes[static_cast<size_t>(state_->attrIndex)].first;
    return ev.name;
}

std::string XmlReader::getValueProperty() const {
    if (!hasCurrentNode(state_.get()))
        return {};
    const auto& ev = state_->events[static_cast<size_t>(state_->pos)];
    if (state_->attrIndex >= 0 &&
        state_->attrIndex < static_cast<int>(ev.attributes.size()))
        return ev.attributes[static_cast<size_t>(state_->attrIndex)].second;
    return ev.value;
}

std::string XmlReader::getLocalNameProperty() const {
    const std::string name = getNameProperty();
    const std::size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

std::string XmlReader::getPrefixProperty() const {
    const std::string name = getNameProperty();
    const std::size_t colon = name.find(':');
    return colon == std::string::npos ? std::string() : name.substr(0, colon);
}

SharpRuntime::intcs XmlReader::getDepthProperty() const {
    if (!hasCurrentNode(state_.get()))
        return 0;
    const auto& ev = state_->events[static_cast<size_t>(state_->pos)];
    return ev.depth + (state_->attrIndex >= 0 ? 1 : 0);
}

bool XmlReader::getHasAttributesProperty() const {
    return getAttributeCountProperty() > 0;
}

SharpRuntime::intcs XmlReader::getAttributeCountProperty() const {
    if (!hasCurrentNode(state_.get()))
        return 0;
    const auto& ev = state_->events[static_cast<size_t>(state_->pos)];
    if (ev.type != XmlNodeType::Element) return 0;
    return static_cast<SharpRuntime::intcs>(ev.attributes.size());
}

bool XmlReader::HasLineInfo() const { return true; }

SharpRuntime::intcs XmlReader::getLineNumberProperty() const {
    if (!hasCurrentNode(state_.get()))
        return 0;
    return state_->events[static_cast<size_t>(state_->pos)].lineNumber;
}

SharpRuntime::intcs XmlReader::getLinePositionProperty() const { return 0; }

std::optional<std::string> XmlReader::LookupNamespace(const std::string& prefix) const {
    if (prefix == "xml") return std::string("http://www.w3.org/XML/1998/namespace");
    if (prefix == "xmlns") return std::string("http://www.w3.org/2000/xmlns/");
    if (!hasCurrentNode(state_.get()))
        return std::nullopt;
    const std::string wanted = prefix.empty() ? std::string("xmlns") : "xmlns:" + prefix;
    for (const tinyxml2::XMLElement* el = state_->events[static_cast<size_t>(state_->pos)].scope;
         el; el = el->Parent() ? el->Parent()->ToElement() : nullptr) {
        if (const char* uri = el->Attribute(wanted.c_str()))
            return std::string(uri);
    }
    return std::nullopt;
}

bool XmlReader::getIsEmptyElementProperty() const {
    if (!hasCurrentNode(state_.get()))
        return false;
    return state_->events[static_cast<size_t>(state_->pos)].isEmptyElement;
}

ReadState XmlReader::getReadStateProperty() const {
    return state_ ? state_->readState : ReadState::Closed;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

bool XmlReader::Read() {
    // The whole of SR-AUD-348: without this guard the cursor advanced AND the next line
    // but one overwrote ReadState::Closed with ReadState::Interactive, so a closed reader
    // both kept traversing and stopped reporting itself as closed.
    if (isClosed(state_.get())) return false;
    state_->attrIndex = -1;
    ++state_->pos;
    if (state_->pos >= static_cast<int>(state_->events.size())) {
        state_->readState = ReadState::EndOfFile;
        return false;
    }
    state_->readState = ReadState::Interactive;
    return true;
}

bool XmlReader::MoveToElement() {
    if (!hasCurrentNode(state_.get()))
        return false;
    state_->attrIndex = -1;
    return state_->events[static_cast<size_t>(state_->pos)].type == XmlNodeType::Element;
}

bool XmlReader::MoveToFirstAttribute() {
    if (!hasCurrentNode(state_.get()))
        return false;
    const auto& ev = state_->events[static_cast<size_t>(state_->pos)];
    if (ev.type != XmlNodeType::Element || ev.attributes.empty()) return false;
    state_->attrIndex = 0;
    return true;
}

static bool isContentNode(XmlNodeType type) {
    return type == XmlNodeType::Element || type == XmlNodeType::EndElement ||
           type == XmlNodeType::Text || type == XmlNodeType::CDATA ||
           type == XmlNodeType::EntityReference || type == XmlNodeType::EndEntity;
}

XmlNodeType XmlReader::MoveToContent() {
    if (isClosed(state_.get())) return XmlNodeType::None;
    if (state_->attrIndex >= 0) MoveToElement();
    // Before the first Read() the reader is on no node; .NET's MoveToContent() advances to
    // the first content node from there as well.
    while (true) {
        if (hasCurrentNode(state_.get())) {
            const XmlNodeType type = state_->events[static_cast<size_t>(state_->pos)].type;
            if (isContentNode(type)) return type;
        }
        if (!Read()) return XmlNodeType::None;
    }
}

bool XmlReader::IsStartElement() {
    return MoveToContent() == XmlNodeType::Element;
}

bool XmlReader::IsStartElement(const std::string& name) {
    return MoveToContent() == XmlNodeType::Element && getNameProperty() == name;
}

void XmlReader::Skip() {
    if (!hasCurrentNode(state_.get())) return;
    if (state_->attrIndex >= 0) MoveToElement();
    const bool container = state_->events[static_cast<size_t>(state_->pos)].type == XmlNodeType::Element &&
                           !state_->events[static_cast<size_t>(state_->pos)].isEmptyElement;
    Read();
    if (!container) return;
    int depth = 0;
    while (hasCurrentNode(state_.get())) {
        const auto& cur = state_->events[static_cast<size_t>(state_->pos)];
        if (cur.type == XmlNodeType::Element && !cur.isEmptyElement) {
            ++depth;
        } else if (cur.type == XmlNodeType::EndElement) {
            if (depth == 0) { Read(); return; }
            --depth;
        }
        Read();
    }
}

bool XmlReader::MoveToNextAttribute() {
    if (!hasCurrentNode(state_.get()))
        return false;
    const auto& ev = state_->events[static_cast<size_t>(state_->pos)];
    if (ev.type != XmlNodeType::Element) return false;
    int next = state_->attrIndex + 1;
    if (next >= static_cast<int>(ev.attributes.size())) return false;
    state_->attrIndex = next;
    return true;
}

std::string XmlReader::GetAttribute(const std::string& name) const {
    if (!hasCurrentNode(state_.get()))
        return {};
    const auto& ev = state_->events[static_cast<size_t>(state_->pos)];
    for (const auto& [k, v] : ev.attributes)
        if (k == name) return v;
    return {};
}

std::string XmlReader::ReadElementContentAsString() {
    if (isClosed(state_.get())) return {};
    std::string result;
    // advance past the Element node we're sitting on
    Read();
    // Tracks nesting depth of any child elements encountered along the way. Without this, a
    // nested child element's OWN EndElement event was indistinguishable from the enclosing
    // element's end -- e.g. for "<a><b>x</b>y</a>", the loop would hit <b>'s EndElement first,
    // treat it as </a>, break early (returning "x" instead of "xy"), and leave the reader
    // positioned mid-content (on the Text("y") node) rather than past </a> -- corrupting every
    // subsequent Read() call, not just the returned string.
    int depth = 0;
    while (state_->pos >= 0 &&
           state_->pos < static_cast<int>(state_->events.size())) {
        const auto& ev = state_->events[static_cast<size_t>(state_->pos)];
        if (ev.type == XmlNodeType::EndElement) {
            if (depth == 0) {
                Read(); // consume our own end-element
                break;
            }
            --depth;
        } else if (ev.type == XmlNodeType::Element) {
            if (!ev.isEmptyElement) ++depth; // self-closing elements have no matching EndElement
        } else if (depth == 0 && (ev.type == XmlNodeType::Text || ev.type == XmlNodeType::CDATA ||
                                  ev.type == XmlNodeType::Whitespace ||
                                  ev.type == XmlNodeType::SignificantWhitespace)) {
            result += ev.value;
        }
        Read();
    }
    return result;
}

// A closed reader is on no node, so these two report the same "not on the right node"
// error they already report for any other wrong position -- no new exception identity.
SharpRuntime::intcs XmlReader::ReadContentAsBase64(std::vector<SharpRuntime::bytecs>& buffer,
                                                   SharpRuntime::intcs index,
                                                   SharpRuntime::intcs count) {
    if (index < 0)
        throw System::ArgumentOutOfRangeException("index");
    if (count < 0)
        throw System::ArgumentOutOfRangeException("count");
    if (static_cast<std::size_t>(index) + static_cast<std::size_t>(count) > buffer.size())
        throw System::ArgumentException(
            "XmlReader::ReadContentAsBase64: the range runs past the end of the buffer.");
    if (!hasCurrentNode(state_.get()) || count == 0)
        return 0;

    if (state_->base64Event != state_->pos) {
        state_->base64Event = state_->pos;
        state_->base64Consumed = 0;
    }

    const std::vector<SharpRuntime::bytecs> decoded =
        System::Convert::FromBase64String(getValueProperty());
    if (state_->base64Consumed >= decoded.size())
        return 0;

    const std::size_t available = decoded.size() - state_->base64Consumed;
    const std::size_t taken = std::min(available, static_cast<std::size_t>(count));
    std::copy(decoded.begin() + static_cast<std::ptrdiff_t>(state_->base64Consumed),
              decoded.begin() + static_cast<std::ptrdiff_t>(state_->base64Consumed + taken),
              buffer.begin() + index);
    state_->base64Consumed += taken;
    return static_cast<SharpRuntime::intcs>(taken);
}

SharpRuntime::intcs XmlReader::ReadContentAsBinHex(std::vector<SharpRuntime::bytecs>& buffer,
                                                   SharpRuntime::intcs index,
                                                   SharpRuntime::intcs count) {
    if (index < 0)
        throw System::ArgumentOutOfRangeException("index");
    if (count < 0)
        throw System::ArgumentOutOfRangeException("count");
    if (static_cast<std::size_t>(index) + static_cast<std::size_t>(count) > buffer.size())
        throw System::ArgumentException(
            "XmlReader::ReadContentAsBinHex: the range runs past the end of the buffer.");
    if (!hasCurrentNode(state_.get()) || count == 0)
        return 0;

    if (state_->base64Event != state_->pos) {
        state_->base64Event = state_->pos;
        state_->base64Consumed = 0;
    }

    const std::string hex = getValueProperty();
    if (hex.size() % 2 != 0)
        throw XmlException("XmlReader::ReadContentAsBinHex: the content has an odd number of digits.");

    auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        throw XmlException("XmlReader::ReadContentAsBinHex: the content is not hexadecimal.");
    };

    const std::size_t total = hex.size() / 2;
    if (state_->base64Consumed >= total)
        return 0;

    const std::size_t taken = std::min(total - state_->base64Consumed,
                                       static_cast<std::size_t>(count));
    for (std::size_t i = 0; i < taken; ++i) {
        const std::size_t at = (state_->base64Consumed + i) * 2;
        buffer[static_cast<std::size_t>(index) + i] =
            static_cast<SharpRuntime::bytecs>((nibble(hex[at]) << 4) | nibble(hex[at + 1]));
    }
    state_->base64Consumed += taken;
    return static_cast<SharpRuntime::intcs>(taken);
}

void XmlReader::ReadStartElement() {
    if (!hasCurrentNode(state_.get()) ||
        state_->events[static_cast<size_t>(state_->pos)].type != XmlNodeType::Element)
        throw XmlException("ReadStartElement: not on an element node");
    Read();
}

void XmlReader::ReadStartElement(const std::string& name) {
    if (MoveToContent() != XmlNodeType::Element || getNameProperty() != name)
        throw XmlException("Element '" + name + "' was not found. Line " +
                           std::to_string(getLineNumberProperty()) + ", position " +
                           std::to_string(getLinePositionProperty()) + ".");
    Read();
}

void XmlReader::ReadEndElement() {
    if (!hasCurrentNode(state_.get()) ||
        state_->events[static_cast<size_t>(state_->pos)].type != XmlNodeType::EndElement)
        throw XmlException("ReadEndElement: not on an end-element node");
    Read();
}

void XmlReader::Close() {
    if (state_) state_->readState = ReadState::Closed; // idempotent by construction
}

// ---------------------------------------------------------------------------
// Factory helpers
// ---------------------------------------------------------------------------

static XmlReader* createFromDoc(std::unique_ptr<XmlReaderState> st) {
    if (st->doc.Error())
        throw XmlException(std::string("XmlReader: parse error: ") +
                                 (st->doc.ErrorStr() ? st->doc.ErrorStr() : ""));
    // Walk all top-level nodes (declaration + root element)
    for (tinyxml2::XMLNode* n = st->doc.FirstChild(); n; n = n->NextSibling())
        buildEvents(n, st->events, nullptr, 0);
    return new XmlReader(std::move(st));
}

static void applySettings(XmlReaderState& st, const XmlReaderSettings& settings) {
    std::vector<XmlEvent> kept;
    kept.reserve(st.events.size());
    for (auto& ev : st.events) {
        switch (ev.type) {
        case XmlNodeType::DocumentType:
            if (settings.ProhibitDtd || settings.DtdProcessing == DtdProcessing::Prohibit)
                throw XmlException("For security reasons DTD is prohibited in this XML document. "
                                   "To enable DTD processing set the DtdProcessing property on "
                                   "XmlReaderSettings to Parse and pass the settings into "
                                   "XmlReader.Create method.");
            if (settings.DtdProcessing == DtdProcessing::Ignore) continue;
            break;
        case XmlNodeType::Comment:
            if (settings.IgnoreComments) continue;
            break;
        case XmlNodeType::ProcessingInstruction:
            if (settings.IgnoreProcessingInstructions) continue;
            break;
        case XmlNodeType::Whitespace:
        case XmlNodeType::SignificantWhitespace:
            if (settings.IgnoreWhitespace) continue;
            break;
        default:
            break;
        }
        kept.push_back(std::move(ev));
    }
    st.events = std::move(kept);
}

XmlReader* XmlReader::Create(const std::string& inputUri) {
    auto st = std::make_unique<XmlReaderState>();
    // Heuristic: treat as file path if it ends with .xml or contains a path separator --
    // UNLESS the (whitespace-trimmed) input starts with '<', which is an unambiguous, much
    // stronger signal that this is XML content rather than a path (a file path essentially
    // never starts with '<'). Without this check, ANY XML content containing a '/' character
    // -- extremely common: self-closing tags like "<br/>", URLs in attribute/text content, XML
    // namespace URIs almost always being "http://..." -- was misclassified as a file path and
    // sent to LoadFile(), which fails (no such file) and throws a misleading "parse error" for
    // perfectly valid XML text.
    std::size_t firstNonSpace = inputUri.find_first_not_of(" \t\r\n");
    bool looksLikeContent = firstNonSpace != std::string::npos && inputUri[firstNonSpace] == '<';
    bool isFile = !looksLikeContent &&
                  (inputUri.find('/') != std::string::npos ||
                   inputUri.find('\\') != std::string::npos ||
                   (inputUri.size() >= 4 &&
                    inputUri.substr(inputUri.size() - 4) == ".xml"));
    if (isFile)
        st->doc.LoadFile(inputUri.c_str());
    else
        st->doc.Parse(inputUri.c_str());
    return createFromDoc(std::move(st));
}

XmlReader* XmlReader::Create(const std::string& inputUri, const XmlReaderSettings& settings) {
    std::unique_ptr<XmlReader> reader(Create(inputUri));
    applySettings(*reader->state_, settings);
    return reader.release();
}

XmlReader* XmlReader::CreateFromString(const std::string& xmlContent) {
    auto st = std::make_unique<XmlReaderState>();
    st->doc.Parse(xmlContent.c_str());
    return createFromDoc(std::move(st));
}

} // namespace System::Xml
