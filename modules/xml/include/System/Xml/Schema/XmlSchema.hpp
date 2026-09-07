// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

namespace System::Xml::Schema {

    /**
     * @brief An XSD schema.
     *
     * This is a minimal stub carrying the name and namespace only. The type exists because
     * `System::Xml::Serialization::IXmlSerializable::GetSchema()` is declared to return it, and
     * .NET's own documentation instructs implementers to return `null` from that method rather
     * than a schema. No caller in this runtime reads a schema, so nothing beyond the identity of
     * the type is implemented; the members are added if and when something needs them.
     */
    class XmlSchema {
    public:
        /** @brief Constructs an empty schema. */
        XmlSchema() = default;

        /** @brief Destroys the schema. */
        virtual ~XmlSchema() = default;
    };

} // namespace System::Xml::Schema
