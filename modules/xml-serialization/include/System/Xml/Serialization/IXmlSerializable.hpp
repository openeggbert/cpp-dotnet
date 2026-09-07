// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include "System/Xml/Schema/XmlSchema.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"

namespace System::Xml::Serialization {

    /**
     * @brief Provides custom formatting for XML serialization and deserialization.
     *
     * `XmlSerializer` derives the XML form of a type from its members. A type that implements
     * this interface takes that job over instead: the serializer hands it the reader or writer
     * and the type decides what the document looks like. That is how a type controls a wire
     * format it does not own, and it is the interface a caller uses directly when it drives an
     * `XmlReader`/`XmlWriter` itself rather than going through `XmlSerializer`.
     *
     * @note The two methods are not symmetric about the element that wraps them. `WriteXml`
     * writes the *content* of the element the caller has already opened, and must not write the
     * wrapping element itself; `ReadXml` is positioned on that wrapping element and is
     * responsible for consuming it, including its end tag. Getting this wrong reads or writes
     * one element too few, which is the classic defect in implementations of this interface.
     */
    class IXmlSerializable {
    public:
        /** @brief Destroys the implementation. */
        virtual ~IXmlSerializable() = default;

        /**
         * @brief Returns the schema describing the XML this type produces.
         *
         * @return `nullptr`. .NET reserves this method and instructs implementers to return
         *         null; a type that wants a schema declares it out of band instead.
         */
        [[nodiscard]] virtual Schema::XmlSchema* GetSchema() const { return nullptr; }

        /**
         * @brief Reads the object's state from its XML representation.
         *
         * @param reader Positioned on the element that wraps this object's content. The
         *               implementation consumes that element, including its end tag.
         */
        virtual void ReadXml(Xml::XmlReader& reader) = 0;

        /**
         * @brief Writes the object's state as XML.
         *
         * @param writer Positioned inside the element that wraps this object's content. The
         *               implementation writes the content only, not the wrapping element.
         */
        virtual void WriteXml(Xml::XmlWriter& writer) const = 0;
    };

} // namespace System::Xml::Serialization
