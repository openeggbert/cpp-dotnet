// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Xml/ReadState.hpp"
#include "System/Xml/XmlNodeType.hpp"

namespace System::Xml {

    struct XmlReaderState; ///< Opaque tinyxml2 state; defined in XmlReader.cpp.
    class XmlReaderSettings;

    /**
     * @brief Represents a reader that provides fast, non-cached, forward-only access to XML data.
     *
     * Implemented as a DOM-cursor over a tinyxml2 document tree.  The entire
     * document is parsed on creation; @c Read() advances a flat event list
     * built from the DOM.
     *
     * Partial C++ counterpart of .NET System.Xml.XmlReader.
     *
     * @note Status: IMPLEMENTED — backed by vendored tinyxml2.
     */
    class XmlReader {
        std::unique_ptr<XmlReaderState> state_;

    public:
        /** @brief Internal constructor used by factory methods; prefer @c Create() / @c CreateFromString(). */
        explicit XmlReader(std::unique_ptr<XmlReaderState> s);

        ~XmlReader();

        /** @brief Returns the type of the current node, or @c XmlNodeType::None when the
         *  reader is on no node — before the first @c Read(), past the last one, or after
         *  @c Close(). */
        [[nodiscard]] XmlNodeType getNodeTypeProperty() const;

        /** @brief Returns the qualified name of the current node, or @c "" when the reader
         *  is on no node (including after @c Close()). */
        [[nodiscard]] std::string getNameProperty() const;

        /** @brief Returns the local part of the current node's name — the text after the
         *  namespace prefix's colon, or the whole name when it has no prefix. */
        [[nodiscard]] std::string getLocalNameProperty() const;

        /** @brief Returns the namespace prefix of the current node's name, or @c "" when the
         *  name has none. */
        [[nodiscard]] std::string getPrefixProperty() const;

        /** @brief Returns the depth of the current node: 0 for the document's top-level nodes,
         *  one more for each enclosing element; an attribute is one deeper than its element. */
        [[nodiscard]] SharpRuntime::intcs getDepthProperty() const;

        /** @brief Returns @c true when the current element has at least one attribute. */
        [[nodiscard]] bool getHasAttributesProperty() const;

        /** @brief Returns the number of attributes on the current element, or 0 on any other
         *  node. */
        [[nodiscard]] SharpRuntime::intcs getAttributeCountProperty() const;

        /** @brief Returns the text value of the current node (Text/CDATA/Comment), or @c ""
         *  when the reader is on no node (including after @c Close()). */
        [[nodiscard]] std::string getValueProperty() const;

        /** @brief Returns @c true if the current element was written in the empty-tag
         *  (self-closing, e.g. @c &lt;foo/&gt;) syntax; @c false for @c &lt;foo&gt;&lt;/foo&gt;,
         *  even when it has no children. */
        [[nodiscard]] bool getIsEmptyElementProperty() const;

        /** @brief Returns the current read state. */
        [[nodiscard]] ReadState getReadStateProperty() const;

        /**
         * @brief Advances the reader to the next node.
         *
         * @return @c true if a node was read; @c false at end of document, and @c false once
         *         @c Close() has been called — a closed reader does not advance, and
         *         @c getReadStateProperty() stays @c ReadState::Closed across any number of
         *         further calls, exactly as @c ReadState::EndOfFile already behaves.
         */
        bool Read();

        /**
         * @brief Moves the cursor back to the element node after iterating attributes.
         *
         * @return @c true if the reader is positioned on an element; @c false when the
         *         reader is on no node, including after @c Close().
         */
        bool MoveToElement();

        /**
         * @brief Moves the cursor to the first attribute of the current element.
         *
         * @return @c true if the element has an attribute; @c false on any other node.
         */
        bool MoveToFirstAttribute();

        /**
         * @brief Skips comments, processing instructions, the XML declaration, document type
         *        nodes and whitespace until the reader is on a content node (element,
         *        end element, text or CDATA) or at end of file; an attribute cursor is moved
         *        back to its element first.
         *
         * @return The node type the reader stopped on; @c XmlNodeType::None at end of file.
         */
        XmlNodeType MoveToContent();

        /**
         * @brief Calls @c MoveToContent() and tells whether it stopped on a start element.
         *
         * @return @c true when the current content node is an element.
         */
        bool IsStartElement();

        /**
         * @brief Calls @c MoveToContent() and tells whether it stopped on a start element
         *        with the given qualified name.
         *
         * @param name  The qualified name to match.
         * @return @c true when the current content node is an element named @p name.
         */
        bool IsStartElement(const std::string& name);

        /**
         * @brief Skips the current node and, for a non-empty element, all of its children,
         *        leaving the reader on the node that follows; on an attribute the element
         *        is skipped. Does nothing when the reader is on no node.
         */
        void Skip();

        /**
         * @brief Resolves a namespace prefix in the scope of the current node, exactly as the
         *        @c xmlns declarations on it and its ancestors define it.
         *
         * @param prefix  The prefix to resolve; @c "" asks for the default namespace.
         * @return The namespace URI, or @c std::nullopt when the prefix is not declared in
         *         scope. The @c xml and @c xmlns prefixes resolve to their fixed URIs.
         */
        [[nodiscard]] std::optional<std::string> LookupNamespace(const std::string& prefix) const;

        /** @brief Always @c true: the parser records the line every node starts on. These three
         *  members are the @c IXmlLineInfo contract, offered directly because this reader keeps
         *  no vtable (the class is pinned to a single owning pointer). */
        [[nodiscard]] bool HasLineInfo() const;

        /** @brief Returns the 1-based line the current node starts on, or 0 on no node. */
        [[nodiscard]] SharpRuntime::intcs getLineNumberProperty() const;

        /** @brief Returns 0: the parser does not record the column a node starts in. */
        [[nodiscard]] SharpRuntime::intcs getLinePositionProperty() const;

        /**
         * @brief Moves to the next attribute of the current element.
         *
         * @return @c true if there was a next attribute to move to; @c false when the
         *         reader is on no node, including after @c Close().
         */
        bool MoveToNextAttribute();

        /**
         * @brief Returns the value of an attribute by name on the current element.
         *
         * @param name Attribute local name.
         * @return Attribute value, or empty string if not found or if the reader is on no
         *         node, including after @c Close().
         */
        [[nodiscard]] std::string GetAttribute(const std::string& name) const;

        /**
         * @brief Reads the text content of the current element and advances past its end-element.
         *
         * @return The concatenated text content, or @c "" once @c Close() has been called.
         */
        std::string ReadElementContentAsString();

        /**
         * @brief Verifies that the current node is an element and advances the reader.
         *
         * @throws XmlException if the current node is not an element, which includes a
         *         reader that has been closed — a closed reader is on no node.
         */
        /**
         * @brief Decodes Base64 text content into a byte buffer.
         *
         * Reads from the current text node. Calling it again on the same node continues
         * where the previous call stopped, which is how a caller drains content larger
         * than its buffer; moving the reader elsewhere starts over.
         *
         * @param buffer Destination.
         * @param index  Where in @p buffer to start writing.
         * @param count  The most bytes to write.
         * @return How many bytes were written -- fewer than @p count once the content runs out.
         *
         * @throws System::ArgumentOutOfRangeException when @p index or @p count is negative.
         * @throws System::ArgumentException when the range runs past the end of @p buffer.
         */
        SharpRuntime::intcs ReadContentAsBase64(std::vector<SharpRuntime::bytecs>& buffer,
                                                SharpRuntime::intcs index,
                                                SharpRuntime::intcs count);

        /**
         * @brief Decodes BinHex text content into a byte buffer.
         *
         * The counterpart of @c XmlWriter::WriteBinHex, and positioned the same way as
         * @c ReadContentAsBase64: repeated calls on one node continue where the last stopped.
         * Both hexadecimal cases are accepted on the way in.
         *
         * @param buffer Destination.
         * @param index  Where in @p buffer to start writing.
         * @param count  The most bytes to write.
         * @return How many bytes were written.
         *
         * @throws System::ArgumentOutOfRangeException when @p index or @p count is negative.
         * @throws System::ArgumentException when the range runs past the end of @p buffer.
         * @throws System::Xml::XmlException when the content is not valid BinHex.
         */
        SharpRuntime::intcs ReadContentAsBinHex(std::vector<SharpRuntime::bytecs>& buffer,
                                                SharpRuntime::intcs index,
                                                SharpRuntime::intcs count);

        void ReadStartElement();

        /**
         * @brief Checks, after @c MoveToContent(), that the current node is a start element
         *        with the given qualified name and advances past it.
         *
         * @param name  The qualified name the element must have.
         * @throws XmlException when the current content node is not that element, with the
         *         message @c "Element 'name' was not found. Line L, position P."
         */
        void ReadStartElement(const std::string& name);

        /**
         * @brief Verifies that the current node is an end-element and advances the reader.
         *
         * @throws XmlException if the current node is not an end-element, which includes a
         *         reader that has been closed — a closed reader is on no node.
         */
        void ReadEndElement();

        /**
         * @brief Closes the reader and releases resources.
         *
         * Terminal and idempotent. Afterwards @c getReadStateProperty() is permanently
         * @c ReadState::Closed, the cursor never advances again, and every accessor reports
         * the reader's own "no current node" answer. Before this contract was enforced,
         * @c Read() both advanced past a closed reader and overwrote the closed state with
         * @c ReadState::Interactive.
         */
        void Close();

        /**
         * @brief Creates an XmlReader that reads from a file path or raw XML content.
         *
         * If @p inputUri (after trimming leading whitespace) starts with '<' it is always
         * treated as raw XML text; otherwise, if it looks like a file path (contains '/' or
         * '\' or ends with ".xml") the file is loaded, and it is treated as raw XML text
         * otherwise.
         *
         * @param inputUri  File path or raw XML text.
         * @return Heap-allocated XmlReader; caller owns the pointer.
         * @throws XmlException on parse error.
         */
        static XmlReader* Create(const std::string& inputUri);

        /**
         * @brief Creates an XmlReader as the one-argument overload does, applying @p settings:
         *        @c DtdProcessing::Prohibit rejects a document that carries a DOCTYPE,
         *        @c DtdProcessing::Ignore drops the node, and @c IgnoreComments,
         *        @c IgnoreProcessingInstructions and @c IgnoreWhitespace drop those nodes.
         *
         * @param inputUri  File path or raw XML text.
         * @param settings  The reader settings to apply.
         * @return Heap-allocated XmlReader; caller owns the pointer.
         * @throws XmlException on parse error, or on a DOCTYPE when DTD processing is
         *         prohibited ("For security reasons DTD is prohibited in this XML document. …").
         */
        static XmlReader* Create(const std::string& inputUri, const XmlReaderSettings& settings);

        /**
         * @brief Creates an XmlReader that parses @p xmlContent as raw XML.
         *
         * @param xmlContent  Well-formed XML text.
         * @return Heap-allocated XmlReader; caller owns the pointer.
         * @throws XmlException on parse error.
         */
        static XmlReader* CreateFromString(const std::string& xmlContent);
    };

} // namespace System::Xml
