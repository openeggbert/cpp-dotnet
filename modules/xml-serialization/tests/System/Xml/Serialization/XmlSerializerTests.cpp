// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
//
// SAMPLES-DEC-008 vertical slice. The three types below are transcribed field-for-field from
// the real XNA sources (not invented shapes):
//   - SaveGameDescriptionData  <- RolePlayingGame/RolePlayingGame/Session/SaveGameDescription.cs
//     (three public string fields, no nesting -- the simplest real call site).
//   - EntityData/EntityListData <- ShipGame/ShipGame/EntityList.cs
//     (a public string + a 16-float value type, inside a List<T> field -- exercises value-type
//     flattening and a named collection field in the same shape ShipGame's own EntityList.Save/
//     Load use).
// A root-level List<T> (RolePlayingGame's `new XmlSerializer(typeof(List<...>))` call sites) is
// exercised separately below with a minimal item type.
#include <gtest/gtest.h>

#include "System/Xml/Serialization/XmlSerializer.hpp"
#include "System/IO/MemoryStream.hpp"

using System::Xml::Serialization::XmlSerializationOptions;
using System::Xml::Serialization::XmlSerializer;
using System::Collections::Generic::List;

namespace {

// --- SaveGameDescription: RolePlayingGame/RolePlayingGame/Session/SaveGameDescription.cs -----

struct SaveGameDescriptionData {
    std::string FileName;
    std::string ChapterName;
    std::string Description;

    SHARP_XML_SERIALIZABLE(SaveGameDescriptionData, "SaveGameDescription",
                            SHARP_XML_M(SaveGameDescriptionData, FileName),
                            SHARP_XML_M(SaveGameDescriptionData, ChapterName),
                            SHARP_XML_M(SaveGameDescriptionData, Description))
};

TEST(XmlSerializerStreamTests, DeserializeReadsFromCurrentStreamPositionAndLeavesStreamOpen) {
    const std::string xml = "prefix<?xml version=\"1.0\"?><SaveGameDescription>"
                            "<FileName>slot1</FileName><ChapterName>One</ChapterName>"
                            "<Description>Ready</Description></SaveGameDescription>";
    System::IO::MemoryStream stream(
        reinterpret_cast<const SharpRuntime::bytecs*>(xml.data()),
        static_cast<SharpRuntime::intcs>(xml.size()), false);
    stream.setPositionProperty(6);

    const SaveGameDescriptionData value = XmlSerializer<SaveGameDescriptionData>{}.Deserialize(stream);

    EXPECT_EQ(value.FileName, "slot1");
    EXPECT_EQ(value.ChapterName, "One");
    EXPECT_EQ(value.Description, "Ready");
    EXPECT_TRUE(stream.getCanReadProperty());
}

// --- ShipGame's Entity/EntityList: ShipGame/ShipGame/EntityList.cs ----------------------------

struct MatrixData {
    float M11 = 0, M12 = 0, M13 = 0, M14 = 0;
    float M21 = 0, M22 = 0, M23 = 0, M24 = 0;
    float M31 = 0, M32 = 0, M33 = 0, M34 = 0;
    float M41 = 0, M42 = 0, M43 = 0, M44 = 0;

    SHARP_XML_SERIALIZABLE(MatrixData, "Matrix",
                            SHARP_XML_M(MatrixData, M11), SHARP_XML_M(MatrixData, M12),
                            SHARP_XML_M(MatrixData, M13), SHARP_XML_M(MatrixData, M14),
                            SHARP_XML_M(MatrixData, M21), SHARP_XML_M(MatrixData, M22),
                            SHARP_XML_M(MatrixData, M23), SHARP_XML_M(MatrixData, M24),
                            SHARP_XML_M(MatrixData, M31), SHARP_XML_M(MatrixData, M32),
                            SHARP_XML_M(MatrixData, M33), SHARP_XML_M(MatrixData, M34),
                            SHARP_XML_M(MatrixData, M41), SHARP_XML_M(MatrixData, M42),
                            SHARP_XML_M(MatrixData, M43), SHARP_XML_M(MatrixData, M44))

    static MatrixData Identity() {
        MatrixData m;
        m.M11 = m.M22 = m.M33 = m.M44 = 1.0f;
        return m;
    }

    bool operator==(const MatrixData&) const = default;
};

struct EntityData {
    std::string name;
    MatrixData transform;

    SHARP_XML_SERIALIZABLE(EntityData, "Entity", SHARP_XML_M(EntityData, name), SHARP_XML_M(EntityData, transform))

    bool operator==(const EntityData&) const = default;
};

struct EntityListData {
    std::vector<EntityData> entities;

    SHARP_XML_SERIALIZABLE(EntityListData, "EntityList", SHARP_XML_M(EntityListData, entities))
};

// --- A minimal item type for the root-level List<T> call sites --------------------------------

struct ModifiedChestEntryData {
    std::string chestName;
    bool isTaken = false;

    SHARP_XML_SERIALIZABLE(ModifiedChestEntryData, "ModifiedChestEntry",
                            SHARP_XML_M(ModifiedChestEntryData, chestName),
                            SHARP_XML_M(ModifiedChestEntryData, isTaken))

    bool operator==(const ModifiedChestEntryData&) const = default;
};

// A friend function cannot be *defined* inside a local class, so the negative-control type
// (below) must live at namespace scope like every other registered type here -- not inside the
// TEST body it's used from.
struct SwappedOrderData {
    std::string FileName;
    std::string ChapterName;
    std::string Description;
    SHARP_XML_SERIALIZABLE(SwappedOrderData, "SaveGameDescription",
                            SHARP_XML_M(SwappedOrderData, ChapterName),  // planted: swapped with FileName
                            SHARP_XML_M(SwappedOrderData, FileName), SHARP_XML_M(SwappedOrderData, Description))
};

}  // namespace

// ===============================================================================================
// SaveGameDescription: exact wire-format pin (element names, order, root namespaces), then a
// round trip. The exact-string test is the one a name/order/namespace regression can actually
// fail; the round trip alone could not (see the negative-control test below, which proves it).
// ===============================================================================================

TEST(XmlSerializerTests, SaveGameDescription_SerializesToExactExpectedWire) {
    SaveGameDescriptionData value{"save1.sav", "Chapter 2", "12:34 elapsed"};
    XmlSerializer<SaveGameDescriptionData> serializer;

    std::string xml = serializer.Serialize(value);

    const std::string expected =
        "<?xml version=\"1.0\"?>"
        "<SaveGameDescription xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
        "<FileName>save1.sav</FileName>"
        "<ChapterName>Chapter 2</ChapterName>"
        "<Description>12:34 elapsed</Description>"
        "</SaveGameDescription>";
    EXPECT_EQ(xml, expected);
}

TEST(XmlSerializerTests, SaveGameDescription_RoundTrips) {
    SaveGameDescriptionData value{"save1.sav", "Chapter 2", "12:34 elapsed"};
    XmlSerializer<SaveGameDescriptionData> serializer;

    SaveGameDescriptionData back = serializer.Deserialize(serializer.Serialize(value));

    EXPECT_EQ(back.FileName, value.FileName);
    EXPECT_EQ(back.ChapterName, value.ChapterName);
    EXPECT_EQ(back.Description, value.Description);
}

// A negative control (the test-quality rule this project holds itself to): prove the exact-wire
// test above can actually fail, by planting the exact defect class this module is meant to
// catch -- two members swapped, which is indistinguishable from correct if both are read back
// into the same struct shape and only round-trip equality is asserted.
TEST(XmlSerializerTests, NegativeControl_SwappedFieldOrder_FailsTheExactWireAssertion) {
    SwappedOrderData value{"save1.sav", "Chapter 2", "12:34 elapsed"};
    XmlSerializer<SwappedOrderData> serializer;

    std::string xml = serializer.Serialize(value);

    const std::string wireCorrectOrder =
        "<?xml version=\"1.0\"?>"
        "<SaveGameDescription xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
        "<FileName>save1.sav</FileName>"
        "<ChapterName>Chapter 2</ChapterName>"
        "<Description>12:34 elapsed</Description>"
        "</SaveGameDescription>";

    // Measured: this is exactly the mismatch a real order regression would produce. Round-trip
    // (Deserialize(Serialize(x)) == x) would NOT have caught it, because deserialization looks
    // members up by name, not position -- which is why the exact-wire test above, not just the
    // round trip, is the one worth keeping.
    EXPECT_NE(xml, wireCorrectOrder);
}

// ===============================================================================================
// ShipGame's Entity/EntityList: value-type flattening (Matrix -> 16 child elements, in
// declaration order) and a named collection field.
// ===============================================================================================

TEST(XmlSerializerTests, Entity_FlattensMatrixToSixteenOrderedElements) {
    EntityData entity;
    entity.name = "carrier_01";
    entity.transform = MatrixData::Identity();
    entity.transform.M41 = 200.5f;
    entity.transform.M42 = -75.25f;
    entity.transform.M43 = 12.0f;

    XmlSerializer<EntityData> serializer;
    std::string xml = serializer.Serialize(entity);

    const std::string expected =
        "<?xml version=\"1.0\"?>"
        "<Entity xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
        "<name>carrier_01</name>"
        "<transform>"
        "<M11>1</M11><M12>0</M12><M13>0</M13><M14>0</M14>"
        "<M21>0</M21><M22>1</M22><M23>0</M23><M24>0</M24>"
        "<M31>0</M31><M32>0</M32><M33>1</M33><M34>0</M34>"
        "<M41>200.5</M41><M42>-75.25</M42><M43>12</M43><M44>1</M44>"
        "</transform>"
        "</Entity>";
    EXPECT_EQ(xml, expected);

    EntityData back = serializer.Deserialize(xml);
    EXPECT_TRUE(back == entity);
}

TEST(XmlSerializerTests, EntityList_RoundTripsAMultiEntityCollection) {
    EntityListData list;
    EntityData a;
    a.name = "carrier_01";
    a.transform = MatrixData::Identity();
    EntityData b;
    b.name = "fighter_07";
    b.transform = MatrixData::Identity();
    b.transform.M41 = 10.0f;
    list.entities = {a, b};

    XmlSerializer<EntityListData> serializer;
    std::string xml = serializer.Serialize(list);

    // The collection field is wrapped by its own field name ("entities"), each item named by
    // the item type's registered root name ("Entity") -- XNA's default List<T> field behavior,
    // distinct from the ArrayOf-prefixed name a *root-level* List<T> gets (tested below).
    EXPECT_NE(xml.find("<entities><Entity>"), std::string::npos);
    EXPECT_EQ(xml.find("ArrayOf"), std::string::npos);

    EntityListData back = serializer.Deserialize(xml);
    ASSERT_EQ(back.entities.size(), 2u);
    EXPECT_TRUE(back.entities[0] == a);
    EXPECT_TRUE(back.entities[1] == b);
}

// ===============================================================================================
// Root-level List<T>: RolePlayingGame's `new XmlSerializer(typeof(List<ModifiedChestEntry>))`
// call sites (Session.cs lines ~1733-1868 in the real source). .NET names the root
// "ArrayOf" + the item type's name when List<T>'s T is not itself generic.
// ===============================================================================================

TEST(XmlSerializerTests, RootLevelList_NamesRootArrayOfItemType) {
    std::vector<ModifiedChestEntryData> entries{{"chest_north", true}, {"chest_south", false}};
    XmlSerializer<std::vector<ModifiedChestEntryData>> serializer;

    std::string xml = serializer.Serialize(entries);

    const std::string expected =
        "<?xml version=\"1.0\"?>"
        "<ArrayOfModifiedChestEntry xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
        "<ModifiedChestEntry><chestName>chest_north</chestName><isTaken>true</isTaken></ModifiedChestEntry>"
        "<ModifiedChestEntry><chestName>chest_south</chestName><isTaken>false</isTaken></ModifiedChestEntry>"
        "</ArrayOfModifiedChestEntry>";
    EXPECT_EQ(xml, expected);

    std::vector<ModifiedChestEntryData> back = serializer.Deserialize(xml);
    ASSERT_EQ(back.size(), 2u);
    EXPECT_TRUE(back[0] == entries[0]);
    EXPECT_TRUE(back[1] == entries[1]);
}

TEST(XmlSerializerTests, RootLevelList_EmptyListRoundTrips) {
    std::vector<ModifiedChestEntryData> entries;
    XmlSerializer<std::vector<ModifiedChestEntryData>> serializer;

    std::vector<ModifiedChestEntryData> back = serializer.Deserialize(serializer.Serialize(entries));
    EXPECT_TRUE(back.empty());
}

TEST(XmlSerializerTests, SystemList_UsesTheSameWireContractAsDotNetList) {
    List<ModifiedChestEntryData> entries;
    entries.Add({"chest_north", true});
    entries.Add({"chest_south", false});
    XmlSerializer<List<ModifiedChestEntryData>> serializer;

    const std::string xml = serializer.Serialize(entries);
    EXPECT_NE(xml.find("<ArrayOfModifiedChestEntry "), std::string::npos);
    EXPECT_NE(xml.find("<ModifiedChestEntry><chestName>chest_north</chestName>"),
              std::string::npos);

    const List<ModifiedChestEntryData> roundTrip = serializer.Deserialize(xml);
    ASSERT_EQ(roundTrip.getCountProperty(), 2);
    const ModifiedChestEntryData expectedNorth{"chest_north", true};
    const ModifiedChestEntryData expectedSouth{"chest_south", false};
    EXPECT_TRUE(roundTrip.getItem(0) == expectedNorth);
    EXPECT_TRUE(roundTrip.getItem(1) == expectedSouth);
}

// ===============================================================================================
// .NET's own missing-member semantics: absent element -> member keeps its default. Throwing
// would reject a save file written by an older build of the same game, which is the exact
// compatibility this module exists to provide.
// ===============================================================================================

TEST(XmlSerializerTests, Deserialize_MissingElement_LeavesTheMemberAtItsDefault) {
    XmlSerializer<SaveGameDescriptionData> serializer;
    const std::string xmlMissingDescription =
        "<SaveGameDescription><FileName>a</FileName><ChapterName>b</ChapterName></SaveGameDescription>";

    SaveGameDescriptionData value = serializer.Deserialize(xmlMissingDescription);

    EXPECT_EQ(value.FileName, "a");
    EXPECT_EQ(value.ChapterName, "b");
    EXPECT_EQ(value.Description, "");  // absent, so untouched -- not an error
}

TEST(XmlSerializerTests, Deserialize_UnknownElement_IsIgnored) {
    XmlSerializer<SaveGameDescriptionData> serializer;
    const std::string xmlWithExtra =
        "<SaveGameDescription><FileName>a</FileName><Unexpected>x</Unexpected>"
        "<ChapterName>b</ChapterName><Description>c</Description></SaveGameDescription>";

    SaveGameDescriptionData value = serializer.Deserialize(xmlWithExtra);

    EXPECT_EQ(value.FileName, "a");
    EXPECT_EQ(value.ChapterName, "b");
    EXPECT_EQ(value.Description, "c");
}

// ===============================================================================================
// Item 1: List<primitive>. RolePlayingGame's PartySaveData carries both a List<string> and a
// List<int>, and .NET names their items by the XSD type ("string", "int"), not the C# keyword.
// ===============================================================================================

namespace {

struct PartySaveDataLike {
    std::vector<std::string> monsterKillNames;
    std::vector<std::int32_t> monsterKillCounts;
    std::int32_t partyGold = 0;

    SHARP_XML_SERIALIZABLE(PartySaveDataLike, "PartySaveData",
                            SHARP_XML_M(PartySaveDataLike, monsterKillNames),
                            SHARP_XML_M(PartySaveDataLike, monsterKillCounts),
                            SHARP_XML_M(PartySaveDataLike, partyGold))
};

}  // namespace

TEST(XmlSerializerTests, PrimitiveLists_UseXsdItemNames) {
    PartySaveDataLike party;
    party.monsterKillNames = {"Skeleton", "Bandit"};
    party.monsterKillCounts = {3, 7};
    party.partyGold = 1250;

    XmlSerializer<PartySaveDataLike> serializer;
    std::string xml = serializer.Serialize(party);

    const std::string expected =
        "<?xml version=\"1.0\"?>"
        "<PartySaveData xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
        "<monsterKillNames><string>Skeleton</string><string>Bandit</string></monsterKillNames>"
        "<monsterKillCounts><int>3</int><int>7</int></monsterKillCounts>"
        "<partyGold>1250</partyGold>"
        "</PartySaveData>";
    EXPECT_EQ(xml, expected);

    PartySaveDataLike back = serializer.Deserialize(xml);
    EXPECT_EQ(back.monsterKillNames, party.monsterKillNames);
    EXPECT_EQ(back.monsterKillCounts, party.monsterKillCounts);
    EXPECT_EQ(back.partyGold, party.partyGold);
}

// ===============================================================================================
// Item 2: enums serialize as their member NAME. RolePlayingGame's PlayerPosition.Direction is
// the reachable case; a numeric cast would produce <Direction>2</Direction> and fail to load in
// the original game.
// ===============================================================================================

namespace {

enum class DirectionLike { North, East, South, West };

SHARP_XML_ENUM(DirectionLike, SHARP_XML_E(DirectionLike, North), SHARP_XML_E(DirectionLike, East),
                SHARP_XML_E(DirectionLike, South), SHARP_XML_E(DirectionLike, West))

struct PointLike {
    std::int32_t X = 0;
    std::int32_t Y = 0;
    SHARP_XML_SERIALIZABLE(PointLike, "Point", SHARP_XML_M(PointLike, X), SHARP_XML_M(PointLike, Y))
    bool operator==(const PointLike&) const = default;
};

struct PlayerPositionLike {
    PointLike TilePosition;
    DirectionLike Direction = DirectionLike::South;

    SHARP_XML_SERIALIZABLE(PlayerPositionLike, "PlayerPosition",
                            SHARP_XML_M(PlayerPositionLike, TilePosition),
                            SHARP_XML_M(PlayerPositionLike, Direction))
};

}  // namespace

TEST(XmlSerializerTests, Enum_SerializesAsMemberName) {
    PlayerPositionLike position;
    position.TilePosition = {12, -4};
    position.Direction = DirectionLike::West;

    XmlSerializer<PlayerPositionLike> serializer;
    std::string xml = serializer.Serialize(position);

    const std::string expected =
        "<?xml version=\"1.0\"?>"
        "<PlayerPosition xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
        "<TilePosition><X>12</X><Y>-4</Y></TilePosition>"
        "<Direction>West</Direction>"
        "</PlayerPosition>";
    EXPECT_EQ(xml, expected);

    PlayerPositionLike back = serializer.Deserialize(xml);
    EXPECT_TRUE(back.TilePosition == position.TilePosition);
    EXPECT_EQ(back.Direction, DirectionLike::West);
}

TEST(XmlSerializerTests, Enum_EveryEnumeratorRoundTrips) {
    XmlSerializer<PlayerPositionLike> serializer;
    for (DirectionLike direction :
         {DirectionLike::North, DirectionLike::East, DirectionLike::South, DirectionLike::West}) {
        PlayerPositionLike position;
        position.Direction = direction;
        EXPECT_EQ(serializer.Deserialize(serializer.Serialize(position)).Direction, direction);
    }
}

TEST(XmlSerializerTests, Enum_UnknownName_Throws) {
    XmlSerializer<PlayerPositionLike> serializer;
    EXPECT_THROW(serializer.Deserialize("<PlayerPosition><Direction>Sideways</Direction></PlayerPosition>"),
                  System::Xml::XmlException);
}

// ===============================================================================================
// Item 6: an inherited member is registered exactly like a declared one. RolePlayingGame's
// WorldEntry<T> : MapEntry<T> : ContentEntry<T> chain is the reachable case, and .NET emits the
// base members first -- which is what listing them first here produces.
// ===============================================================================================

namespace {

struct ContentEntryLike {
    std::string ContentName;
};

struct MapEntryLike : ContentEntryLike {
    PointLike MapPosition;
    DirectionLike Direction = DirectionLike::South;
};

struct WorldEntryOfChestLike : MapEntryLike {
    std::string MapContentName;

    // Base members first, then the derived one -- .NET's own order for an inheritance chain.
    SHARP_XML_SERIALIZABLE(WorldEntryOfChestLike, "WorldEntryOfChest",
                            SHARP_XML_M(WorldEntryOfChestLike, ContentName),
                            SHARP_XML_M(WorldEntryOfChestLike, MapPosition),
                            SHARP_XML_M(WorldEntryOfChestLike, Direction),
                            SHARP_XML_M(WorldEntryOfChestLike, MapContentName))
};

}  // namespace

TEST(XmlSerializerTests, InheritedMembers_SerializeBaseFirstThenDerived) {
    WorldEntryOfChestLike entry;
    entry.ContentName = "Chests/GoldChest";
    entry.MapPosition = {3, 9};
    entry.Direction = DirectionLike::North;
    entry.MapContentName = "Maps/Village";

    XmlSerializer<WorldEntryOfChestLike> serializer;
    std::string xml = serializer.Serialize(entry);

    const std::string expected =
        "<?xml version=\"1.0\"?>"
        "<WorldEntryOfChest xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
        "<ContentName>Chests/GoldChest</ContentName>"
        "<MapPosition><X>3</X><Y>9</Y></MapPosition>"
        "<Direction>North</Direction>"
        "<MapContentName>Maps/Village</MapContentName>"
        "</WorldEntryOfChest>";
    EXPECT_EQ(xml, expected);
}

// The generic-instantiation naming that earlier looked like it would need name-mangling
// machinery: because the root name is a registered string, a root-level list of
// WorldEntry<Chest> already produces .NET's ArrayOfWorldEntryOfChest with no extra mechanism.
// This is one of RolePlayingGame's real Session.cs call sites.
TEST(XmlSerializerTests, RootLevelListOfGenericInstantiation_NamesArrayOfWorldEntryOfChest) {
    std::vector<WorldEntryOfChestLike> entries(1);
    entries[0].ContentName = "Chests/GoldChest";
    entries[0].MapContentName = "Maps/Village";

    XmlSerializer<std::vector<WorldEntryOfChestLike>> serializer;
    std::string xml = serializer.Serialize(entries);

    EXPECT_NE(xml.find("<ArrayOfWorldEntryOfChest "), std::string::npos);
    EXPECT_NE(xml.find("<WorldEntryOfChest>"), std::string::npos);

    std::vector<WorldEntryOfChestLike> back = serializer.Deserialize(xml);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back[0].ContentName, "Chests/GoldChest");
    EXPECT_EQ(back[0].MapContentName, "Maps/Village");
}

// ===============================================================================================
// Items 4 and 5: declaration form and indentation.
// ===============================================================================================

TEST(XmlSerializerFormattingTests, DeclarationDefaultsToNoEncodingAttribute) {
    SaveGameDescriptionData value{"a", "b", "c"};
    XmlSerializer<SaveGameDescriptionData> serializer;

    // The authentic fixtures open with a bare declaration; that is the default here.
    EXPECT_TRUE(serializer.Serialize(value).starts_with("<?xml version=\"1.0\"?>"));

    XmlSerializationOptions withEncoding;
    withEncoding.WriteEncodingAttribute = true;
    EXPECT_TRUE(serializer.Serialize(value, withEncoding)
                     .starts_with("<?xml version=\"1.0\" encoding=\"utf-8\"?>"));

    XmlSerializationOptions omitted;
    omitted.OmitXmlDeclaration = true;
    EXPECT_TRUE(serializer.Serialize(value, omitted).starts_with("<SaveGameDescription"));
}

TEST(XmlSerializerFormattingTests, IndentedOutputRoundTripsToTheSameValues) {
    EntityListData list;
    EntityData entity;
    entity.name = "spawn0";
    entity.transform = MatrixData::Identity();
    list.entities = {entity};

    XmlSerializer<EntityListData> serializer;
    XmlSerializationOptions indented;
    indented.Indent = true;
    std::string xml = serializer.Serialize(list, indented);

    EXPECT_NE(xml.find("\n"), std::string::npos);
    EXPECT_NE(xml.find("    <entities>"), std::string::npos);

    // Whitespace must not change what is read back -- this is what makes the authentic,
    // hand-indented fixtures loadable.
    EntityListData back = serializer.Deserialize(xml);
    ASSERT_EQ(back.entities.size(), 1u);
    EXPECT_TRUE(back.entities[0] == entity);
}

// --- C# reference members, modelled as std::shared_ptr -------------------------------------
//
// Measured against the XNA 4.0 runtime with RolePlayingGameData's own types: a null reference
// MEMBER is omitted entirely, while a null list ITEM is written as xsi:nil="true". Both shapes
// are reproduced here rather than smoothed into one.

namespace {

struct RefLeaf {
    std::string Name;
    int Value = 0;
    SHARP_XML_SERIALIZABLE(RefLeaf, "RefLeaf", SHARP_XML_M(RefLeaf, Name), SHARP_XML_M(RefLeaf, Value))
    bool operator==(const RefLeaf&) const = default;
};

struct RefHolder {
    std::shared_ptr<RefLeaf> Sprite;
    std::vector<std::shared_ptr<RefLeaf>> Animations;
    SHARP_XML_SERIALIZABLE(RefHolder, "RefHolder", SHARP_XML_M(RefHolder, Sprite),
                           SHARP_XML_M(RefHolder, Animations))
};

}  // namespace

TEST(XmlSerializerTests, ReferenceMemberSerializesThePointeeInline) {
    RefHolder holder;
    holder.Sprite = std::make_shared<RefLeaf>(RefLeaf{"Chest", 3});
    holder.Animations.push_back(std::make_shared<RefLeaf>(RefLeaf{"Idle", 1}));

    const std::string xml = XmlSerializer<RefHolder>{}.Serialize(holder);

    EXPECT_NE(xml.find("<Sprite><Name>Chest</Name><Value>3</Value></Sprite>"), std::string::npos) << xml;
    EXPECT_NE(xml.find("<Animations><RefLeaf><Name>Idle</Name><Value>1</Value></RefLeaf></Animations>"),
              std::string::npos)
        << xml;
}

TEST(XmlSerializerTests, NullReferenceMemberIsOmittedAndNullItemIsNil) {
    RefHolder holder;
    holder.Animations.push_back(nullptr);

    const std::string xml = XmlSerializer<RefHolder>{}.Serialize(holder);

    EXPECT_EQ(xml.find("<Sprite"), std::string::npos) << "a null member must not be written: " << xml;
    // The nil marker is .NET's; the empty-element spelling is this module's own XmlWriter form
    // (`<X/>`, no space before the slash), which every other element here already uses.
    EXPECT_NE(xml.find("<RefLeaf xsi:nil=\"true\"/>"), std::string::npos) << xml;
}

TEST(XmlSerializerTests, ReferenceRoundTripRestoresPointeesAndNulls) {
    RefHolder holder;
    holder.Sprite = std::make_shared<RefLeaf>(RefLeaf{"Chest", 3});
    holder.Animations.push_back(std::make_shared<RefLeaf>(RefLeaf{"Idle", 1}));
    holder.Animations.push_back(nullptr);

    const RefHolder back = XmlSerializer<RefHolder>{}.Deserialize(XmlSerializer<RefHolder>{}.Serialize(holder));

    ASSERT_NE(back.Sprite, nullptr);
    EXPECT_EQ(*back.Sprite, *holder.Sprite);
    ASSERT_EQ(back.Animations.size(), 2u);
    ASSERT_NE(back.Animations[0], nullptr);
    EXPECT_EQ(*back.Animations[0], *holder.Animations[0]);
    EXPECT_EQ(back.Animations[1], nullptr);
}
