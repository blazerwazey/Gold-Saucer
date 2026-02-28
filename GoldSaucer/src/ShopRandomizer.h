#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QTextStream>
#include <random>

class Randomizer;

// ─── FF7.exe shop data structures (matches exe binary layout) ───────────────

// Shop types as stored in the exe (2-byte enum)
enum class ExeShopType : quint16 {
    Item       = 0,
    Weapon     = 1,
    Item2      = 2,
    Materia    = 3,
    General    = 4,
    Vegetable  = 5,   // Chocobo greens – leave alone
    Accessory  = 6,
    Tool       = 7,
    Hotel      = 8    // Not a real shop – skip
};

// One slot inside a shop (8 bytes in the exe)
struct ExeShopSlot {
    qint32  type;       // 0 = item/weapon/armor/accessory, 1 = materia
    quint16 index;      // item index (encoding depends on type)
    quint16 padding;
};

// One shop record (84 bytes in the exe)
struct ExeShopRecord {
    enum { SLOT_COUNT = 10, RECORD_BYTES = 84 };   // 2+1+1 + 10*8

    ExeShopType shopType;
    quint8      itemCount;        // number of populated slots (max 10)
    ExeShopSlot entries[10];
};

// ─── ShopRandomizer ─────────────────────────────────────────────────────────

class ShopRandomizer
{
public:
    explicit ShopRandomizer(Randomizer* parent);
    ~ShopRandomizer() = default;

    bool randomize();

private:
    Randomizer*    m_parent;
    std::mt19937&  m_rng;

    // ── exe location & I/O ──────────────────────────────────────────────
    static const int NUM_SHOPS = 80;

    // English Steam offsets (base – no language offset for English)
    static const qint64 SHOP_INVENTORY_POS = 0x521E18;

    QString findFF7Exe() const;
    bool    readShops (const QString& exePath, QVector<ExeShopRecord>& shops);
    bool    generateHextPatch(const QString& outputPath, const QVector<ExeShopRecord>& shops);

    // ── randomization logic ─────────────────────────────────────────────
    void randomizeShop(ExeShopRecord& shop, QTextStream& log);

    quint16 randomItem()      const;
    quint16 randomWeapon()    const;
    quint16 randomArmor()     const;
    quint16 randomAccessory() const;
    quint16 randomMateria()   const;
    quint16 randomFromCategory(ExeShopType shopType) const;

    // ── item-ID ranges (non-materia composite index) ────────────────────
    static const quint16 ITEM_COUNT       = 105;  // 0x00 – 0x68
    static const quint16 WEAPON_START     = 128;   // 0x80
    static const quint16 WEAPON_COUNT     = 128;
    static const quint16 ARMOR_START      = 256;   // 0x100
    static const quint16 ARMOR_COUNT      = 32;
    static const quint16 ACCESSORY_START  = 288;   // 0x120
    static const quint16 ACCESSORY_COUNT  = 32;
    static const quint16 MATERIA_COUNT    = 91;    // 0x00 – 0x5A

    // ── shop names for debug log ────────────────────────────────────────
    static QString shopName(int shopId);
};
