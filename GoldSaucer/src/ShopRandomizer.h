#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QTextStream>
#include <QSet>
#include <QPair>
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

    // The shop-table live VA is the same across known ff7_en.exe builds (used in
    // the FFNx Hext, which patches live memory). Only the FILE offsets shift
    // between builds — classic Steam (6.42 MB) vs the 2026 re-release (5.99 MB) —
    // so the shop-table file offset is detected by probing the candidates below;
    // the price tables keep fixed deltas from it in every build.
    static const qint64 SHOP_INVENTORY_VA   = 0x923418;
    static const qint64 SHOP_POS_CLASSIC    = 0x521E18;  // 6.42 MB Steam build
    static const qint64 SHOP_POS_2026       = 0x521C18;  // 5.99 MB 2026 re-release
    static const qint64 ITEM_PRICE_DELTA    = 0x1A40;    // m_shopPos + this
    static const qint64 MATERIA_PRICE_DELTA = 0x2040;

    // Detected per exe in detectExeLayout(); default to the classic build.
    qint64 m_shopPos         = SHOP_POS_CLASSIC;
    qint64 m_itemPricePos    = SHOP_POS_CLASSIC + ITEM_PRICE_DELTA;
    qint64 m_materiaPricePos = SHOP_POS_CLASSIC + MATERIA_PRICE_DELTA;

    bool    detectExeLayout(const QString& exePath, QTextStream& log);
    QString findFF7Exe() const;
    bool    readShops (const QString& exePath, QVector<ExeShopRecord>& shops);
    bool    generateHextPatch(const QString& outputPath, const QVector<ExeShopRecord>& shops);

    // ── Archipelago shop slots (native-grid Tier-3 AP shops) ────────────
    // Read the .apff7 "shops" array: each AP shop slot makes shop `shop_id`
    // sell item `token_id` (reserved from normal stock; the client detects the
    // purchase + shows the AP name). Filled by loadApShops(), applied after the
    // normal randomization pass.
    void loadApShops(QTextStream& log);
    void applyApShops(QVector<ExeShopRecord>& shops, QTextStream& log);

    struct ApShopSlot { int shopId; quint16 token; bool isMateria; };
    QVector<ApShopSlot> m_apShops;
    QSet<quint16>       m_reservedTokens;   // composite item tokens (slot type 0)
    QSet<quint16>       m_reservedMateria;  // materia tokens        (slot type 1)

    // ── randomization logic (price-tiered pools) ────────────────────────
    void    randomizeShop(int shopId, ExeShopRecord& shop, QTextStream& log);
    quint16 randomFromCategory(ExeShopType shopType, int tier) const;

    // Real prices read from the exe drive both validity (unsellable items have a
    // sentinel price of 1–2) and tiering (early shops sell cheap, late expensive).
    bool readPrices(const QString& exePath, QTextStream& log);
    void buildTieredPools(QTextStream& log);
    int  shopTier(int shopId) const;            // 0 = early, 1 = mid, 2 = late
    quint16 pickTiered(int category, int tier) const;

    // ── composite item-ID ranges (non-materia) ──────────────────────────
    static const quint16 ITEM_COUNT       = 105;  // 0x00 – 0x68
    static const quint16 WEAPON_START     = 128;   // 0x80
    static const quint16 WEAPON_COUNT     = 128;
    static const quint16 ARMOR_START      = 256;   // 0x100
    static const quint16 ARMOR_COUNT      = 32;
    static const quint16 ACCESSORY_START  = 288;   // 0x120
    static const quint16 ACCESSORY_COUNT  = 32;
    static const quint16 MATERIA_MAX_ID   = 0x5A;  // MasterSummon (highest valid)
    static const int     COMPOSITE_COUNT  = 0x180; // entries in the item-price table

    // Price tables are u32 arrays at m_itemPricePos / m_materiaPricePos (detected).
    static const quint32 SELLABLE_MIN      = 11;       // price <= this = unsellable sentinel

    enum Category { CatItem = 0, CatWeapon, CatArmor, CatAccessory, CatMateria, CatCOUNT };
    static const int NUM_TIERS = 3;

    QVector<quint32> m_itemPrices;     // composite id  -> price
    QVector<quint32> m_materiaPrices;  // materia id     -> price
    QVector<quint16> m_pool[CatCOUNT][NUM_TIERS];  // [category][tier] -> eligible ids

    // ── shop names for debug log ────────────────────────────────────────
    static QString shopName(int shopId);
};
