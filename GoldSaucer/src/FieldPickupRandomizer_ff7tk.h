#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTextStream>
#include <QVector>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QRandomGenerator>
#include <QFile>
#include <QDir>
#include <QDebug>
#include "MakouLgpManager.h"

class Randomizer;

// Bank parsing macros from Makou Reactor (Opcode.h lines 31-32)
#define B1(v)             ((v >> 4) & 0xF)
#define B2(v)             (v & 0xF)

// Makou-compatible packed opcode structures
#pragma pack(push, 1)

// STITM (0x58) – Mirrors OpcodeItem in Makou Reactor Opcode.h lines 814-818
struct OpcodeSTITMRaw {
    quint8 id;        // 0x58
    quint8 banks;     // B1 = itemID bank, B2 = quantity bank
    quint16 itemID;   // little-endian item ID (bank 1)
    quint8 quantity;  // quantity (bank 2)
};

// SMTRA (0x5B) – Mirrors OpcodeMateria in Makou Reactor Opcode.h lines 824-828
struct OpcodeSMTRARaw {
    quint8 id;           // 0x5B
    quint8 banks[2];     // banks[0]: B1=materiaID bank, B2=AP bank1
                         // banks[1]: B1=AP bank2, B2=AP bank3
    quint8 materiaID;    // materia ID (bank 1)
    quint8 APCount[3];   // AP value bytes (bank 2, bank 3, bank 4)
};

#pragma pack(pop)

// Holds a found STITM opcode and its location within field data
struct STITMInfo {
    int offset;
    quint16 originalItemID;
    quint8 originalQuantity;
    quint8 banks;
    bool isDirectValue;       // true when banks==0 (literal, not variable ref)

    STITMInfo() : offset(-1), originalItemID(0), originalQuantity(0),
                  banks(0), isDirectValue(false) {}
};

// Holds a found SMTRA opcode and its location within field data
struct SMTRAInfo {
    int offset;
    quint8 originalMateriaID;
    quint8 originalAP[3];
    quint8 banks[2];
    bool isDirectValue;       // true when both banks==0

    SMTRAInfo() : offset(-1), originalMateriaID(0), banks{0, 0},
                  isDirectValue(false) { originalAP[0] = originalAP[1] = originalAP[2] = 0; }
};

// Tracks a single opcode modification for text updating
struct OpcodeModification {
    int opcodeOffset;       // absolute offset in decompressed data
    QString newName;        // new item/materia display name
    bool isMateria;         // true for SMTRA, false for STITM

    OpcodeModification() : opcodeOffset(-1), isMateria(false) {}
    OpcodeModification(int off, const QString& name, bool mat)
        : opcodeOffset(off), newName(name), isMateria(mat) {}
};

// Main Field Pickup Randomizer Class
class FieldPickupRandomizer_ff7tk : public QObject
{
    Q_OBJECT

public:
    explicit FieldPickupRandomizer_ff7tk(Randomizer* parent = nullptr);
    ~FieldPickupRandomizer_ff7tk();

    // Entry point called by Randomizer::randomizeFieldPickups()
    bool randomize();

    // Item pool helpers (public so tests can call them)
    void initializeItemPools();
    quint16 getRandomItem(int rarityMode);

    void setDebugMode(bool enabled) { m_debugMode = enabled; }

private:
    Randomizer* m_parent;
    QRandomGenerator m_rng;
    bool m_debugMode;

    // Item pools by rarity tier
    QVector<quint16> m_commonItems;
    QVector<quint16> m_uncommonItems;
    QVector<quint16> m_rareItems;
    QVector<quint16> m_veryRareItems;

    // Materia pool
    QVector<quint8> m_materiaPool;

    // --- Key item structs (must be declared before processFieldFile) ---
    struct GlobalKeyItem {
        int fileIndex;
        int scriptOffset;
        quint8 bankByte;
        quint8 address;
        quint8 bit;
    };
    struct GlobalStitmLocation {
        int fileIndex;
        int scriptOffset;
        int minGameMoment;
        int maxGameMoment;
        bool isBiton{false};
    };
    struct KeyItemPlacement {
        GlobalKeyItem keyItem;
        QString keyName;
        int targetOffset;   // offset in target field (was STITM)
        bool targetIsBiton{false};
    };
    struct KeyItemFieldMod {
        QVector<int>               bitonNopOffsets;  // original BITONs to NOP
        QVector<KeyItemPlacement>  placements;       // new BITONs to write
    };

    enum class WardrobeCategory {
        None = 0,
        Dress,
        Wig,
        Tiara,
        Cologne,
        Underwear,
    };

    // --- Core workflow ---
    bool processFieldFile(const QString& fieldName, QByteArray& fieldData,
                          QTextStream& debugStream,
                          const KeyItemFieldMod* keyItemMod = nullptr);

    // --- STITM scanning ---
    QVector<STITMInfo> scanForSTITM(const QByteArray& fieldData,
                                     const QString& fieldName,
                                     QTextStream& debugStream);
    bool validateSTITM(const STITMInfo& info) const;
    bool applySTITMRandomization(STITMInfo& info, QByteArray& fieldData,
                                  quint16 newItemID, QTextStream& debugStream);

    // --- SMTRA scanning ---
    QVector<SMTRAInfo> scanForSMTRA(const QByteArray& fieldData,
                                     const QString& fieldName,
                                     QTextStream& debugStream);
    bool validateSMTRA(const SMTRAInfo& info) const;
    bool applySMTRARandomization(SMTRAInfo& info, QByteArray& fieldData,
                                  quint8 newMateriaID, QTextStream& debugStream);

    // --- Text section update ---
    bool updateFieldTexts(QByteArray& decompressed,
                          const QVector<OpcodeModification>& modifications,
                          QTextStream& debugStream);
    static const int MESSAGE_OPCODE = 0x40;

    void collectKeyItemsAndStitm(const QByteArray& fieldData, int fileIndex,
                                  const QString& fieldName,
                                  QMap<quint32, GlobalKeyItem>& uniqueKeyItems,
                                  QVector<GlobalStitmLocation>& stitmLocations,
                                  QTextStream& debugStream);
    QMap<QString, KeyItemFieldMod> performKeyItemSwaps(
                             QMap<quint32, GlobalKeyItem>& uniqueKeyItems,
                             QVector<GlobalStitmLocation>& stitmLocations,
                             const QStringList& allFileNames,
                             QTextStream& debugStream);

    static int getFieldSphere(const QString& fieldName);
    static int getKeyItemMinSphere(quint32 keyItemId);
    static int getKeyItemMaxSphere(quint32 keyItemId);
    static int getKeyItemMinMoment(quint32 keyItemId);
    static int getKeyItemMaxMoment(quint32 keyItemId);
    static QPair<int, int> getStitmMomentWindow(const QString& fieldName, int scriptOffset);
    static QPair<int, int> getFieldMomentWindow(const QString& fieldName);
    static WardrobeCategory getWardrobeCategory(quint32 keyItemId);
    static QString wardrobeCategoryName(WardrobeCategory category);
    static bool requiresMirroredBitons(const QString& fieldName);
    static QString getKeyItemName(quint16 saveOffset, quint8 bit);

    // --- Helpers ---
    void buildItemPools();
    void buildMateriaPool();
    quint8 getRandomMateria();
    QString getItemName(quint16 itemId) const;
    QString getMateriaName(quint8 materiaId) const;
    QString findFlevelPath() const;

    // --- Constants ---
    static const int MAX_ITEM_ID = 319;
    static const int MAX_MATERIA_ID = 90;
    static const int STITM_OPCODE = 0x58;
    static const int STITM_SIZE = 5;
    static const int SMTRA_OPCODE = 0x5B;
    static const int SMTRA_SIZE = 7;
    static const int BITON_OPCODE = 0x82;
    static const QString DEBUG_FILE_NAME;
};
