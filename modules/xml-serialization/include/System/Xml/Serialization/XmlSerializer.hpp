// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "System/Collections/Generic/List.hpp"
#include "System/IO/StreamReader.hpp"
#include "System/Xml/Serialization/detail/XmlLeafConvert.hpp"
#include "System/Xml/Serialization/detail/XmlMember.hpp"
#include "System/Xml/XmlDocument.hpp"
#include "System/Xml/XmlElement.hpp"
#include "System/Xml/XmlException.hpp"
#include "System/Xml/XmlWriter.hpp"
#include "System/Xml/XmlWriterSettings.hpp"

namespace System::Xml::Serialization {

    /**
     * @brief Formatting knobs for `XmlSerializer::Serialize`.
     *
     * Defaults produce the compact form, which is what the exact-wire tests pin because it is
     * the only fully deterministic one.
     *
     * @note **Whitespace is not part of the compatibility contract, and the authentic fixtures
     * prove it.** `Samples/Spacewar_4_0/settings.xml` is indented with two spaces and
     * `Samples/ShipGame_4_0/.../level1_spawns.xml` with four -- both are genuine
     * `XmlSerializer` output shipped in the same official XNA Game Studio source tree. So the
     * element names, their order, their text and the root's namespace declarations are the
     * contract; the indentation is not. `Indent` emits the four-space form this serializer has
     * always written, set explicitly through `XmlWriterSettings::IndentChars`.
     */
    struct XmlSerializationOptions {
        /** @brief Pretty-print with tinyxml2's fixed four-space indentation. */
        bool Indent = false;
        /** @brief Write `encoding="utf-8"` in the declaration. The authentic fixtures do not:
         * they open with a bare `<?xml version="1.0"?>`, which is what .NET emits when
         * serializing to a `TextWriter` rather than a `Stream`. */
        bool WriteEncodingAttribute = false;
        /** @brief Omit the XML declaration entirely. */
        bool OmitXmlDeclaration = false;
    };

    /**
     * @brief Provides methods for serializing objects to XML and deserializing XML to objects.
     *
     * C++ counterpart of .NET `System.Xml.Serialization.XmlSerializer`, scoped to what
     * `SAMPLES-DEC-008` actually needs: the closed set of save-data types reachable from
     * ShipGame's `EntityList`/`LightList`, Spacewar's `Settings` and RolePlayingGame's
     * `Session` (see `docs/XmlSerializationScope.md` for the inventory and the evidence that
     * none of them use `[XmlInclude]`/xsi:type polymorphism).
     *
     * @note Real .NET discovers a type's serializable members and their order via reflection.
     * That is a permanent deviation here (CLAUDE.md), so a type opts in explicitly with
     * `SHARP_XML_SERIALIZABLE(Type, "RootElementName", SHARP_XML_M(Type, member1), ...)` --
     * the same compile-time-customization-point shape `JsonSerializer` already uses via
     * `nlohmann`'s ADL hooks, just with no vendored library underneath since none exists for
     * XML object-mapping (`tinyxml2` is a parser, not a mapper).
     *
     * **Deliberately out of scope** (tracked in `docs/XmlSerializationScope.md`, not silently
     * missing): `[XmlInclude]`/xsi:type polymorphic dispatch, `[XmlArray]`/`[XmlArrayItem]`
     * overrides, `[XmlAttribute]`-mapped members and circular-reference detection. None is
     * exercised by any reachable call site in the three samples.
     */
    template <typename T>
    class XmlSerializer {
    public:
        XmlSerializer() = default;

        /** @brief Serializes @p value to an XML document string, root element carrying the
         * `xsi`/`xsd` namespace declarations .NET's default `XmlSerializer` always writes. */
        [[nodiscard]] std::string Serialize(const T& value,
                                             const XmlSerializationOptions& options = {}) const {
            System::Xml::XmlDocument doc;
            BuildDocument(doc, value);
            return Render(doc, options);
        }

        /** @brief Parses @p xml produced by (or wire-compatible with) `Serialize`, back into a
         * @p T. @throws System::Xml::XmlException on malformed XML. */
        [[nodiscard]] T Deserialize(const std::string& xml) const {
            System::Xml::XmlDocument doc;
            doc.LoadXml(xml);
            System::Xml::XmlElement* root = doc.getDocumentElementProperty();
            if (root == nullptr) {
                throw System::Xml::XmlException("XmlSerializer::Deserialize: no root element.");
            }

            T result{};
            if constexpr (detail::IsXmlListV<T>) {
                ReadList(root, result);
            } else {
                static_assert(detail::XmlComposite<T>,
                              "XmlSerializer<T>::Deserialize: T must be SHARP_XML_SERIALIZABLE, "
                              "or a std::vector of one.");
                ReadMembers(root, result);
            }
            return result;
        }

        /**
         * @brief Deserializes an XML document read from the stream's current position.
         *
         * @param stream Readable stream containing the XML document.
         * @return Deserialized value.
         * @throws System::ArgumentException if the stream is not readable.
         * @throws System::Xml::XmlException if the XML is malformed.
         */
        [[nodiscard]] T Deserialize(System::IO::Stream& stream) const {
            System::IO::StreamReader reader(&stream, true);
            return Deserialize(reader.ReadToEnd());
        }

        /**
         * @brief Appends @p value to @p parent as one element, inside a document the caller owns.
         *
         * This is the shape RolePlayingGame's save routes actually use: sixteen of the twenty
         * `XmlSerializer` call sites in `Session.cs` are
         * `new XmlSerializer(typeof(X)).Serialize(xmlWriter, value)` against an **already open**
         * writer, nesting several serialized objects inside one `<rolePlayingGameSaveData>`
         * document rather than each producing a document of its own.
         *
         * The element carries the `xsi`/`xsd` declarations, because .NET writes them on the
         * element it creates regardless of the surrounding namespace scope, and it carries no
         * XML declaration, because the enclosing document already has one.
         */
        void SerializeInto(System::Xml::XmlDocument& doc, System::Xml::XmlElement* parent,
                            const T& value) const {
            if constexpr (detail::IsXmlListV<T>) {
                using Item = typename detail::XmlListTraits<T>::Item;
                System::Xml::XmlElement* root =
                    doc.CreateElement(std::string("ArrayOf") + ItemElementName<Item>());
                AddSchemaNamespaces(root);
                parent->AppendChild(root);
                for (const Item& item : value) {
                    WriteItem(doc, root, ItemElementName<Item>(), item);
                }
            } else {
                static_assert(detail::XmlComposite<T>,
                              "XmlSerializer<T>::SerializeInto: T must be SHARP_XML_SERIALIZABLE, "
                              "or a std::vector of one.");
                System::Xml::XmlElement* root =
                    doc.CreateElement(SharpXmlRootName(static_cast<const T*>(nullptr)));
                AddSchemaNamespaces(root);
                parent->AppendChild(root);
                WriteMembers(doc, root, value);
            }
        }

        /**
         * @brief Reads @p value out of an element the caller located in its own document -- the
         * read counterpart of `SerializeInto`, and what `Deserialize(xmlReader)` amounts to once
         * the enclosing document has been parsed.
         */
        [[nodiscard]] T DeserializeFrom(System::Xml::XmlElement* element) const {
            if (element == nullptr) {
                throw System::Xml::XmlException("XmlSerializer::DeserializeFrom: null element.");
            }
            T result{};
            if constexpr (detail::IsXmlListV<T>) {
                ReadList(element, result);
            } else {
                static_assert(detail::XmlComposite<T>,
                              "XmlSerializer<T>::DeserializeFrom: T must be "
                              "SHARP_XML_SERIALIZABLE, or a std::vector of one.");
                ReadMembers(element, result);
            }
            return result;
        }

        /** @brief The element name this type serializes as at document or fragment root --
         * `"PlayerPosition"`, `"ArrayOfModifiedChestEntry"`, and so on. Lets a caller locate its
         * own nodes without duplicating the `ArrayOf` rule. */
        [[nodiscard]] static std::string RootElementName() {
            if constexpr (detail::IsXmlListV<T>) {
                return std::string("ArrayOf") +
                       ItemElementName<typename detail::XmlListTraits<T>::Item>();
            } else {
                return SharpXmlRootName(static_cast<const T*>(nullptr));
            }
        }

    private:
        // --- document assembly ---------------------------------------------------------------

        static void BuildDocument(System::Xml::XmlDocument& doc, const T& value) {
            if constexpr (detail::IsXmlListV<T>) {
                using Item = typename detail::XmlListTraits<T>::Item;
                std::string rootName = std::string("ArrayOf") + ItemElementName<Item>();
                System::Xml::XmlElement* root = MakeRootElement(doc, rootName);
                for (const Item& item : value) {
                    WriteItem(doc, root, ItemElementName<Item>(), item);
                }
            } else {
                static_assert(detail::XmlComposite<T>,
                              "XmlSerializer<T>::Serialize: T must be SHARP_XML_SERIALIZABLE, or "
                              "a std::vector of one.");
                System::Xml::XmlElement* root =
                    MakeRootElement(doc, SharpXmlRootName(static_cast<const T*>(nullptr)));
                WriteMembers(doc, root, value);
            }
        }

        /**
         * @brief Renders the built tree.
         *
         * The indented path goes through `XmlWriter`, whose `Save` does not emit the
         * declaration node, so the declaration is prepended here; the compact path uses
         * `OuterXml`, which does. Both produce the same declaration text for the same options,
         * which `XmlSerializerFormattingTests` pins.
         */
        [[nodiscard]] static std::string Render(System::Xml::XmlDocument& doc,
                                                 const XmlSerializationOptions& options) {
            std::string declaration;
            if (!options.OmitXmlDeclaration) {
                declaration = options.WriteEncodingAttribute ? "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                                                              : "<?xml version=\"1.0\"?>";
            }

            if (!options.Indent) {
                return declaration + doc.getOuterXmlProperty();
            }

            System::Xml::XmlWriterSettings settings;
            settings.Indent = true;
            settings.IndentChars = "    ";
            std::unique_ptr<System::Xml::XmlWriter> writer(
                System::Xml::XmlWriter::CreateToString(settings));
            doc.Save(*writer);
            std::string body = writer->ToString();
            if (declaration.empty()) return body;
            return declaration + "\n" + body;
        }

        /** @brief The two namespace declarations .NET's XmlSerializer writes on every root it
         * creates -- including one nested inside an already-open document. */
        static void AddSchemaNamespaces(System::Xml::XmlElement* element) {
            element->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
            element->SetAttribute("xmlns:xsd", "http://www.w3.org/2001/XMLSchema");
        }

        [[nodiscard]] static System::Xml::XmlElement* MakeRootElement(System::Xml::XmlDocument& doc,
                                                                        const std::string& name) {
            System::Xml::XmlElement* root = doc.CreateElement(name);
            AddSchemaNamespaces(root);
            doc.AppendChild(root);
            return root;
        }

        /** @brief The element name each item of a list gets: the registered root name for a
         * composite, or the XSD primitive name (`string`, `int`, `boolean`, ...) otherwise. */
        template <typename Item>
        [[nodiscard]] static constexpr const char* ItemElementName() {
            if constexpr (detail::IsXmlReferenceV<Item>) {
                return ItemElementName<typename detail::XmlReferenceTraits<Item>::Pointee>();
            } else if constexpr (detail::XmlComposite<Item>) {
                return SharpXmlRootName(static_cast<const Item*>(nullptr));
            } else {
                return detail::XmlPrimitiveElementName<Item>();
            }
        }

        // --- write side --------------------------------------------------------------------

        template <typename Value>
        static void WriteValue(System::Xml::XmlDocument& doc, System::Xml::XmlElement* parent,
                                const std::string& elementName, const Value& value) {
            if constexpr (detail::IsXmlReferenceV<Value>) {
                // A C# reference member. .NET omits a null member entirely and marks a null list
                // item with xsi:nil, both measured against the XNA 4.0 runtime; the caller tells
                // the two apart, so this writes nothing and ListWriteValue handles the item case.
                if (value != nullptr) {
                    WriteValue(doc, parent, elementName, *value);
                }
            } else if constexpr (detail::IsXmlListV<Value>) {
                System::Xml::XmlElement* wrapper = doc.CreateElement(elementName);
                parent->AppendChild(wrapper);
                using Item = typename detail::XmlListTraits<Value>::Item;
                for (const Item& item : value) {
                    WriteItem(doc, wrapper, ItemElementName<Item>(), item);
                }
            } else if constexpr (detail::XmlComposite<Value>) {
                System::Xml::XmlElement* element = doc.CreateElement(elementName);
                parent->AppendChild(element);
                WriteMembers(doc, element, value);
            } else {
                System::Xml::XmlElement* element = doc.CreateElement(elementName);
                parent->AppendChild(element);
                element->setInnerTextProperty(detail::ToXmlText(value));
            }
        }

        /**
         * @brief Writes one list item, which differs from a member only in how a null reference
         * is spelled: .NET omits a null member and writes `xsi:nil="true"` for a null item.
         */
        template <typename Item>
        static void WriteItem(System::Xml::XmlDocument& doc, System::Xml::XmlElement* parent,
                              const std::string& elementName, const Item& item) {
            if constexpr (detail::IsXmlReferenceV<Item>) {
                if (item == nullptr) {
                    System::Xml::XmlElement* element = doc.CreateElement(elementName);
                    element->SetAttribute("xsi:nil", "true");
                    parent->AppendChild(element);
                    return;
                }
                WriteValue(doc, parent, elementName, *item);
            } else {
                WriteValue(doc, parent, elementName, item);
            }
        }

        /** @brief True when an element carries .NET's `xsi:nil="true"` null marker. */
        [[nodiscard]] static bool IsNilElement(System::Xml::XmlElement* element) {
            return element != nullptr && element->GetAttribute("xsi:nil") == "true";
        }

        template <typename Composite>
        static void WriteMembers(System::Xml::XmlDocument& doc, System::Xml::XmlElement* element,
                                  const Composite& value) {
            auto members = SharpXmlMembers(static_cast<const Composite*>(nullptr));
            std::apply(
                [&](const auto&... member) { (WriteValue(doc, element, member.name, value.*(member.ptr)), ...); },
                members);
        }

        // --- read side -----------------------------------------------------------------------

        /** @brief The first *element* child of @p parent named @p name, or nullptr. Text and
         * whitespace nodes are skipped by name comparison, which is what lets an indented
         * fixture (every authentic one is indented) parse identically to a compact one. */
        [[nodiscard]] static System::Xml::XmlElement* FindChildElement(System::Xml::XmlNode* parent,
                                                                        const std::string& name) {
            for (System::Xml::XmlNode* child = parent->getFirstChildProperty(); child != nullptr;
                 child = child->getNextSiblingProperty()) {
                if (child->getNameProperty() == name) {
                    return static_cast<System::Xml::XmlElement*>(child);
                }
            }
            return nullptr;
        }

        template <typename List>
        static void ReadList(System::Xml::XmlNode* parent, List& out) {
            using Item = typename detail::XmlListTraits<List>::Item;
            const std::string itemName = ItemElementName<Item>();
            for (System::Xml::XmlNode* child = parent->getFirstChildProperty(); child != nullptr;
                 child = child->getNextSiblingProperty()) {
                if (child->getNameProperty() != itemName) continue;
                Item item{};
                ReadInto(static_cast<System::Xml::XmlElement*>(child), item);
                detail::XmlListTraits<List>::Append(out, std::move(item));
            }
        }

        template <typename Value>
        static void ReadInto(System::Xml::XmlElement* element, Value& out) {
            if constexpr (detail::IsXmlReferenceV<Value>) {
                // xsi:nil="true" is .NET's own spelling for a null reference; anything else is a
                // real object, so one is constructed to read into -- the C++ counterpart of the
                // instance XmlSerializer creates before filling it.
                if (IsNilElement(element)) {
                    out = nullptr;
                    return;
                }
                using Pointee = typename detail::XmlReferenceTraits<Value>::Pointee;
                out = std::make_shared<Pointee>();
                ReadInto(element, *out);
            } else if constexpr (detail::IsXmlListV<Value>) {
                ReadList(element, out);
            } else if constexpr (detail::XmlComposite<Value>) {
                ReadMembers(element, out);
            } else {
                out = detail::FromXmlText<Value>(element->getInnerTextProperty());
            }
        }

        /**
         * @brief Reads every registered member that is present.
         *
         * **A missing element leaves the member at its default and is not an error**, which is
         * .NET's own behaviour: `XmlSerializer` only reports a missing member when it is
         * declared required by a schema, and none of the sample save types declare anything of
         * the kind. Throwing here instead would reject a real save file written by an older
         * build of the same game -- exactly the compatibility this module exists to provide.
         * `XmlSerializerTests.Deserialize_MissingElement_LeavesTheMemberAtItsDefault` pins it.
         */
        template <typename Composite>
        static void ReadMembers(System::Xml::XmlElement* element, Composite& out) {
            auto members = SharpXmlMembers(static_cast<const Composite*>(nullptr));
            std::apply(
                [&](const auto&... member) {
                    (
                        [&] {
                            System::Xml::XmlElement* found = FindChildElement(element, member.name);
                            if (found == nullptr) return;
                            ReadInto(found, out.*(member.ptr));
                        }(),
                        ...);
                },
                members);
        }
    };

}  // namespace System::Xml::Serialization
