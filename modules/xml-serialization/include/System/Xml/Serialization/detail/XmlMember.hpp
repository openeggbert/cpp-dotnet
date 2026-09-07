// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include <array>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace System::Collections::Generic {
    template <typename T>
    class List;
}

namespace System::Xml::Serialization::detail {

    /**
     * @brief One field/property binding: an explicit XML element name paired with a
     * pointer-to-member, in the order it was registered.
     *
     * SAMPLES-DEC-008's compile-time customization-point design (CLAUDE.md: reflection is a
     * permanent deviation). A real .NET `XmlSerializer` discovers members and their declaration
     * order via reflection; here the order and the name are both spelled out once, at the call
     * site that registers the type, by `SHARP_XML_M` below. That is the same shape
     * `JsonSerializer`'s ADL customization points already use for `nlohmann`'s `to_json`/
     * `from_json` -- a per-type opt-in the compiler checks, not a stub.
     *
     * @note A pointer-to-member may name an **inherited** member (`&Derived::BaseField`), so a
     * C# inheritance chain such as RolePlayingGame's `WorldEntry<T> : MapEntry<T> :
     * ContentEntry<T>` is registered by listing the base members first and the derived members
     * after -- which is exactly the element order .NET emits for such a chain. No separate
     * base-class mechanism is needed, and `XmlSerializerInheritanceTests` pins that.
     */
    template <typename Class, typename Member>
    struct XmlMember {
        const char* name;
        Member Class::*ptr;
    };

    template <typename Class, typename Member>
    [[nodiscard]] constexpr XmlMember<Class, Member> MakeMember(const char* name, Member Class::*ptr) {
        return XmlMember<Class, Member>{name, ptr};
    }

    /** @brief One `name <-> enumerator` pair, as registered by `SHARP_XML_E`. */
    template <typename Enum>
    struct XmlEnumEntry {
        const char* name;
        Enum value;
    };

    /**
     * @brief True for `T = std::vector<U>`, false otherwise.
     *
     * A field of this shape is XNA's un-adorned `List<U>`: no `[XmlArray]`/`[XmlArrayItem]`
     * override in any of the three DEC-008 samples (grep confirmed zero `XmlInclude`/
     * `XmlArray` attributes across ShipGame and RolePlayingGame's Session/save types), so the
     * default naming rule below is the only one this module implements.
     */
    template <typename T>
    struct IsXmlList : std::false_type {};

    template <typename U, typename A>
    struct IsXmlList<std::vector<U, A>> : std::true_type {};

    template <typename U>
    struct IsXmlList<System::Collections::Generic::List<U>> : std::true_type {};

    template <typename T>
    inline constexpr bool IsXmlListV = IsXmlList<T>::value;

    template <typename T>
    struct XmlListTraits;

    template <typename U, typename A>
    struct XmlListTraits<std::vector<U, A>> {
        using Item = U;

        static void Append(std::vector<U, A>& list, U value) {
            list.push_back(std::move(value));
        }
    };

    template <typename U>
    struct XmlListTraits<System::Collections::Generic::List<U>> {
        using Item = U;

        static void Append(System::Collections::Generic::List<U>& list, U value) {
            list.Add(value);
        }
    };

    /**
     * @brief Detects a C# *reference* member, which this port models as `std::shared_ptr<U>`.
     *
     * XmlSerializer follows a reference and writes the object it points at inline. A null one is
     * omitted where it is a member, and written as `xsi:nil="true"` where it is a list item --
     * both measured against the XNA 4.0 runtime with RolePlayingGameData's own types, not
     * inferred. That asymmetry is .NET's, so it is reproduced rather than smoothed over.
     */
    template <typename T>
    struct IsXmlReference : std::false_type {};

    template <typename U>
    struct IsXmlReference<std::shared_ptr<U>> : std::true_type {};

    template <typename T>
    inline constexpr bool IsXmlReferenceV = IsXmlReference<T>::value;

    template <typename T>
    struct XmlReferenceTraits;

    template <typename U>
    struct XmlReferenceTraits<std::shared_ptr<U>> {
        using Pointee = U;
    };

    /**
     * @brief Detects a type that opted in via `SHARP_XML_SERIALIZABLE` -- i.e. one for which
     * `SharpXmlRootName`/`SharpXmlMembers` are found by argument-dependent lookup on a
     * `const T*`.
     *
     * Deliberately excludes any inheritance-based dispatch: XmlSerializer resolves this
     * exclusively on the *declared* type, which is exactly the ADL lookup below and exactly
     * what XNA's own `XmlSerializer` does when no `[XmlInclude]` is present (see
     * `docs/XmlSerializationScope.md`'s "no xsi:type" note).
     */
    template <typename T>
    concept XmlComposite = requires(const T* value) {
        { SharpXmlRootName(value) };
        { SharpXmlMembers(value) };
    };

    /** @brief Detects an enum registered with `SHARP_XML_ENUM`. */
    template <typename T>
    concept XmlEnum = std::is_enum_v<T> && requires(const T* value) {
        { SharpXmlEnumEntries(value) };
    };

}  // namespace System::Xml::Serialization::detail

/**
 * @def SHARP_XML_M
 * @brief Registers one member of a `SHARP_XML_SERIALIZABLE` type: element name equals the C#
 * field/property name it mirrors (XNA's own default -- `XmlSerializer` uses the member's
 * declared name verbatim when no `[XmlElement(ElementName=...)]` is present).
 */
#define SHARP_XML_M(TypeName, member) \
    ::System::Xml::Serialization::detail::MakeMember(#member, &TypeName::member)

/**
 * @def SHARP_XML_SERIALIZABLE
 * @brief Opts @p TypeName into `System::Xml::Serialization::XmlSerializer`: declares the root
 * element name it uses when serialized as a top-level type, and the ordered list of members
 * (build each with `SHARP_XML_M`) walked for both serialize and deserialize.
 *
 * Place inside the class body. A friend function cannot be *defined* inside a local class, so
 * a registered type must live at namespace or class scope, never inside a function body.
 *
 * The root name is an explicit string rather than anything derived from the C++ type name.
 * That is what makes a C# generic instantiation expressible: `WorldEntry<Chest>` registers as
 * `"WorldEntryOfChest"`, and a root-level `std::vector` of it then serializes as
 * `<ArrayOfWorldEntryOfChest>` with no name-mangling machinery -- .NET's own naming, spelled
 * out once instead of guessed from C++ RTTI (which is neither stable nor .NET-shaped).
 */
#define SHARP_XML_SERIALIZABLE(TypeName, rootElementName, ...)                 \
    friend constexpr const char* SharpXmlRootName(const TypeName*) {           \
        return rootElementName;                                                \
    }                                                                          \
    friend constexpr auto SharpXmlMembers(const TypeName*) {                   \
        return std::make_tuple(__VA_ARGS__);                                   \
    }

/**
 * @def SHARP_XML_E
 * @brief One enumerator of a `SHARP_XML_ENUM` registration. The XML text is the enumerator's
 * own name, which is what .NET writes for an enum member with no `[XmlEnum(Name=...)]`.
 */
#define SHARP_XML_E(EnumType, value) \
    ::System::Xml::Serialization::detail::XmlEnumEntry<EnumType> { #value, EnumType::value }

/**
 * @def SHARP_XML_ENUM
 * @brief Opts an enum into XmlSerializer by listing its enumerators (build each with
 * `SHARP_XML_E`). Place at namespace scope, in the enum's own namespace, so ADL finds it.
 *
 * .NET serializes an enum as its member *name*, not its numeric value
 * (`Direction.South` -> `<Direction>South</Direction>`), which is why a name table is required
 * and a numeric cast would be wrong.
 */
#define SHARP_XML_ENUM(EnumType, ...)                                    \
    inline constexpr auto SharpXmlEnumEntries(const EnumType*) {         \
        return std::array{__VA_ARGS__};                                  \
    }
