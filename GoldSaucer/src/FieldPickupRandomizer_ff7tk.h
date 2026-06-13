#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTextStream>
#include <QVector>
#include <QMap>
#include <QHash>
#include <QQueue>
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

    // --- Archipelago BITON mode ---
    struct ApBitonEntry {
        QString  field;
        int      offset;
        bool     isMateria;
        quint16  originalItemId;    // STITM: item index 0-319
        quint8   originalMateriaId; // SMTRA: materia index 0-90
        QString  originalName;
        quint8   bankByte;          // 0x10 (key items, bank 1) or 0x30 (bank 3)
        quint8   address;           // savemap address within the dest bank
        quint8   bit;               // bit 0..7
    };
    QVector<ApBitonEntry> m_apBitonEntries;
    // JSON-driven lookup: key = "fieldname|item_text" (both lowercased)
    // Value is a queue of (bank, address, bit) triples — multiple items with
    // the same name in the same field are consumed in the order they appear
    // in the JSON.  Bank is preserved so we can route key-item placements to
    // bank 1 (vanilla key-item flag) and auto-allocated AP locations to
    // bank 3 (the safe range, away from FF7's busy bank-1 NPC state vars).
    struct ApBitonCoord { quint8 bank; quint8 address; quint8 bit; };
    QHash<QString, QQueue<ApBitonCoord>> m_apJsonLookup;
    // Tracks the most-recently dequeued BITON per (field|item_text) key so
    // that duplicate SMTRA/STITM opcodes (e.g. NPC dialogue branch + actual
    // pickup both producing the same materia) can fall back to a shared
    // BITON when the JSON queue is exhausted.  Without this, only the first
    // matching opcode in script order tracks the location.
    QHash<QString, ApBitonCoord> m_apJsonLastBiton;

    bool loadApJson(const QString& path, QTextStream& debugStream);
    bool applySTITMAsArchipelago(STITMInfo& info, QByteArray& fieldData,
                                 const QString& fieldName, QTextStream& debugStream);
    bool applySMTRAAsArchipelago(SMTRAInfo& info, QByteArray& fieldData,
                                 const QString& fieldName, QTextStream& debugStream);
    void writeArchipelagoSidecar(const QString& outputPath, QTextStream& debugStream) const;

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

    // --- Vanilla BITON replacement for AP mode ---
    int replaceVanillaBitonsForAP(QByteArray& decompressed,
                                   const QString& fieldName,
                                   QTextStream& debugStream);

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

    // --- Free Roam MAPJUMP injection ---
    bool injectFreeRoamMapJump(QByteArray& decompressed, const QString& fieldName,
                               QTextStream& debugStream);
    // Debug-only: dump a field's section-0 entity script table and a decoded
    // opcode listing for each script, to diagnose autonomous entry events
    // (e.g. the Rocket Town soft-lock at game moment 1603).
    void dumpFieldScripts(const QByteArray& decompressed, const QString& fieldName,
                          QTextStream& debugStream);
    // Overwrite an existing (never-shown in Free Roam) field dialog in place with
    // the given FF7-encoded text. Returns the dialog id used, or -1 if no slot is
    // large enough (caller then skips the message). In-place only — no resizing.
    int overwriteFieldDialog(QByteArray& decompressed, const QByteArray& encoded,
                             QTextStream& debugStream);

    // --- Helpers ---
    void buildItemPools();
    void buildMateriaPool();
    quint8 getRandomMateria();
    QString getItemName(quint16 itemId) const;
    QString getMateriaName(quint8 materiaId) const;
    QString findFlevelPath() const;

    // --- Constants ---
    static const int    MAX_ITEM_ID        = 319;
    static const int    MAX_MATERIA_ID     = 90;
    static const int    STITM_OPCODE       = 0x58;
    static const int    STITM_SIZE         = 5;
    static const int    SMTRA_OPCODE       = 0x5B;
    static const int    SMTRA_SIZE         = 7;
    static const int    BITON_OPCODE       = 0x82;
    static const int    BITON_SIZE         = 4;
    // AP_BITON bank/address are sourced per-placement from the .apff7 JSON
    // (see ApBitonCoord).  The default for auto-allocated locations is bank 1
    // (see json_export.py), with a blacklist of known NPC quest-state addresses
    // (0xA0-0xA5, 0xE1-0xE2) to avoid vanilla side-effects.  Key items still
    // resolve to bank 1 / 0x40..0x46 via key_item_biton_map.
    static const quint8 AP_BITON_BANK_BYTE_BANK1 = 0x10;  // dest=bank1, src=bank0
    static const QString DEBUG_FILE_NAME;
};
