// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Xml/XmlWriterSettings.hpp"

namespace System::Xml {

    struct XmlWriterState; ///< Opaque tinyxml2 state; defined in XmlWriter.cpp.

    /**
     * @brief Represents a writer that provides a fast, non-cached, forward-only way to generate XML.
     *
     * Builds a tinyxml2 DOM tree in memory.  Call @c ToString() to obtain the
     * serialized XML, or @c Close() to write it to the file supplied to @c Create().
     *
     * Partial C++ counterpart of .NET System.Xml.XmlWriter.
     *
     * @note Status: IMPLEMENTED — backed by vendored tinyxml2.
     */
    class XmlWriter {
        std::unique_ptr<XmlWriterState> state_;

        explicit XmlWriter(std::unique_ptr<XmlWriterState> s);

    public:
        ~XmlWriter();

        /**
         * @brief Writes the XML declaration (@c <?xml version="1.0"?>).
         *
         * Optional; may be called at most once, before any elements.
         */
        void WriteStartDocument();

        /** @brief Finalises the document (no-op for tinyxml2 DOM, flushes to file if path was set). */
        void WriteEndDocument();

        /**
         * @brief Opens a new element with the given local name.
         *
         * @param localName  Element tag name; validated with @c XmlConvert::VerifyName, the
         *                   same validator @c XmlDocument::CreateElement already uses, so the
         *                   writer can no longer emit a name its own @c XmlReader rejects.
         * @throws System::Xml::XmlException if @p localName is not a valid XML name.
         * @throws System::ArgumentException if @p localName is empty.
         * @throws System::InvalidOperationException if @c Close() has already been called.
         */
        void WriteStartElement(const std::string& localName);

        /**
         * @brief Closes the most recently opened element.
         *
         * @throws System::InvalidOperationException if no element is open, or if @c Close()
         *         has already been called. An unbalanced call was previously discarded in
         *         silence, so the emitted nesting was not the nesting the caller wrote.
         */
        void WriteEndElement();

        /**
         * @brief Closes the innermost open element with an explicit end tag.
         *
         * @c WriteEndElement() lets an element with no content collapse to `<name />`.
         * This one always writes `<name></name>`. The difference is invisible to an XML
         * parser and highly visible to a hand-written reader that counts nodes, which is
         * why .NET offers both and why a format defined by such a reader may depend on it.
         *
         * @throws System::InvalidOperationException when no element is open.
         */
        void WriteFullEndElement();

        /**
         * @brief Writes an attribute on the current element.
         *
         * Must be called immediately after @c WriteStartElement, before any child
         * content or @c WriteEndElement.
         *
         * @param name   Attribute name; validated with @c XmlConvert::VerifyName.
         * @param value  Attribute value.
         * @throws System::Xml::XmlException if @p name is not a valid XML name.
         * @throws System::ArgumentException if @p name is empty.
         * @throws System::InvalidOperationException if no element is open, or if @c Close()
         *         has already been called.
         */
        void WriteAttributeString(const std::string& name, const std::string& value);

        /**
         * @brief Writes a text node as child of the current element.
         *
         * @param text  Text content (will be XML-escaped).
         */
        void WriteString(const std::string& text);

        /**
         * @brief Writes a range of bytes as Base64 text content.
         *
         * @param buffer The bytes to encode.
         * @param index  Index of the first byte to encode.
         * @param count  How many bytes to encode.
         *
         * @throws System::ArgumentOutOfRangeException when @p index or @p count is negative.
         * @throws System::ArgumentException when the range runs past the end of @p buffer.
         */
        void WriteBase64(const std::vector<SharpRuntime::bytecs>& buffer,
                         SharpRuntime::intcs index, SharpRuntime::intcs count);

        /**
         * @brief Writes XML whitespace as content at the current position.
         *
         * @p whitespace may contain only space, tab, carriage return, and line feed. This is
         * distinct from WriteString so document-level whitespace can be represented and invalid
         * document text is rejected.
         *
         * @param whitespace XML whitespace content.
         * @throws System::ArgumentException if @p whitespace contains a non-whitespace character.
         */
        void WriteWhitespace(const std::string& whitespace);

        /**
         * @brief Writes a complete element: @c <name>value</name>.
         *
         * Equivalent to WriteStartElement + WriteString + WriteEndElement.
         *
         * @param name   Element tag name.
         * @param value  Text content.
         */
        void WriteElementString(const std::string& name, const std::string& value);

        /**
         * @brief Writes a comment node: @c <!-- text -->.
         *
         * If @p text contains @c "--" or ends with @c '-', a space is silently inserted to
         * keep the emitted markup well-formed, matching real .NET's
         * @c XmlEncodedRawTextWriter (which self-heals rather than throwing).
         *
         * @param text  Comment text.
         */
        void WriteComment(const std::string& text);

        /**
         * @brief Writes a CDATA section as child of the current element: @c <![CDATA[text]]>.
         *
         * If @p text contains an embedded @c "]]>", the section is silently split into
         * adjacent CDATA sections around it (matching real .NET) so the terminator never
         * appears mid-content; the original text round-trips unchanged on read-back.
         *
         * @param text  Content; not XML-escaped (that is the point of a CDATA section).
         */
        void WriteCData(const std::string& text);

        /**
         * @brief Writes a processing instruction: @c <?target data?>.
         *
         * If @p data contains @c "?>", a space is silently inserted to keep the emitted
         * markup well-formed, matching real .NET's @c XmlEncodedRawTextWriter.
         *
         * @param target  The PI target name; validated with @c XmlConvert::VerifyName. An
         *                unvalidated target containing @c "?>" used to close the instruction
         *                early and spill its remainder into document-level text.
         * @param data    The PI content (not XML-escaped, matching real XML PI syntax).
         * @throws System::Xml::XmlException if @p target is not a valid XML name.
         * @throws System::ArgumentException if @p target is empty.
         * @throws System::InvalidOperationException if @c Close() has already been called.
         *
         * @note **This runtime cannot read back a processing instruction it writes anywhere
         * except before every other node.** The instruction is emitted correctly in any
         * position and is well-formed XML, but this runtime's own parser accepts one only at
         * the very start of the document — an XML declaration may precede it, and nothing
         * else may, not even a comment. `<root><?p d?></root>`, `<root/><?p d?>` and
         * `<!--c--><?p d?><root/>` all fail to load. So SR-AUD-349's closure property —
         * whatever this writer emits, this module's reader must consume — does **not** hold
         * for this one node kind.
         *
         * This is a limitation of the vendored substrate's node-type model, not a check in
         * the wrong place: `vendor/tinyxml2` has no processing-instruction type at all, so
         * every `<?` becomes an XML *declaration* and inherits the rule that a declaration is
         * allowed only at document level and before anything else. `vendor/` is third-party
         * source and is never edited, and pre-rewriting non-leading `<?...?>` around the
         * substrate would fork its semantics for every document.
         *
         * **.NET has no such limitation**, because its model separates the two: its loader
         * switches on `XmlNodeType.XmlDeclaration` and `XmlNodeType.ProcessingInstruction` as
         * distinct cases in the same general node loop (`XmlLoader.cs:203-209`), which runs
         * for element content and not only for the prolog.
         *
         * Ticket **#2202**; pinned by `XmlWriterValidationTests.Decl2202_*` and, at the
         * Xml.Linq layer, by
         * `XLinqLexicalSerializationTests.ProcessingInstruction_ParserPositionLimitIsSubstrateNotSerialization`.
         */
        void WriteProcessingInstruction(const std::string& target, const std::string& data);

        /**
         * @brief Writes a document type declaration: @c <!DOCTYPE name PUBLIC "..." "..." [subset]>.
         *
         * @param name           The DOCTYPE root element name; validated with
         *                       @c XmlConvert::VerifyName.
         * @param publicId       The public identifier, or "" to omit. Not currently validated;
         *                       a @c '"' in this or in @p systemId, or a @c ']' in
         *                       @p internalSubset, still escapes its quoted literal.
         * @param systemId       The system identifier, or "" to omit.
         * @param internalSubset The internal subset, or "" to omit.
         * @throws System::Xml::XmlException if @p name is not a valid XML name.
         * @throws System::ArgumentException if @p name is empty.
         * @throws System::InvalidOperationException if @c Close() has already been called.
         */
        void WriteDocType(const std::string& name, const std::string& publicId,
                           const std::string& systemId, const std::string& internalSubset);

        /**
         * @brief Returns the serialized XML as a string.
         *
         * Includes the XML declaration if @c WriteStartDocument() was called. Compact
         * (no inserted whitespace) unless the writer was created with
         * @c XmlWriterSettings::Indent set to @c true, matching real .NET's default.
         */
        [[nodiscard]] std::string ToString() const;

        /** @brief Flushes and, if a file path was provided to @c Create(), saves to that file. */
        void Flush();

        /**
         * @brief Same as @c Flush(); releases resources and terminally closes the writer.
         *
         * Idempotent — a second call is a no-op, and the destructor calls it unconditionally.
         * Afterwards every @c Write* member throws @c System::InvalidOperationException; those
         * calls were previously accepted and silently discarded. @c ToString() and @c Flush()
         * stay usable, because @c ToString() is how an in-memory writer's result is read back.
         */
        void Close();

        /**
         * @brief Creates an XmlWriter that serializes to a file.
         *
         * Call @c Close() or @c Flush() to write the file.
         *
         * @param outputFileName  Destination file path.
         * @param settings        Formatting settings; only @c Indent is currently consulted
         *                        (matches real @c XmlWriterSettings' default of @c false —
         *                        compact, non-pretty-printed output).
         * @return Heap-allocated XmlWriter; caller owns the pointer.
         */
        static XmlWriter* Create(const std::string& outputFileName,
                                  const XmlWriterSettings& settings = XmlWriterSettings());

        /**
         * @brief Creates an XmlWriter that serializes to an in-memory string.
         *
         * Use @c ToString() to retrieve the result.
         *
         * @param settings  Formatting settings; only @c Indent is currently consulted
         *                  (matches real @c XmlWriterSettings' default of @c false —
         *                  compact, non-pretty-printed output).
         * @return Heap-allocated XmlWriter; caller owns the pointer.
         */
        static XmlWriter* CreateToString(const XmlWriterSettings& settings = XmlWriterSettings());
    };

} // namespace System::Xml
