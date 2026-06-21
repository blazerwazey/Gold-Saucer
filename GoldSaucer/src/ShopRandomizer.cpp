#include "ShopRandomizer.h"
#include "Randomizer.h"
#include "Config.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

ShopRandomizer::ShopRandomizer(Randomizer* parent)
    : m_parent(parent)
    , m_rng(const_cast<std::mt19937&>(parent->m_rng))
{
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────

bool ShopRandomizer::randomize()
{
    QString outputPath = m_parent->getOutputPath();
    
    // --- debug log ---------------------------------------------------------
    QString logPath = outputPath + "/shop_randomization_debug.txt";
    QFile logFile(logPath);
    bool logOk = logFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream log(&logFile);
    if (logOk) log << "=== Shop Randomization ===\n"
                   << QDateTime::currentDateTime().toString() << "\n\n";
    if (logOk) log << "FF7 Path: " << m_parent->getFF7Path() << "\n";
    if (logOk) log << "Output Path: " << outputPath << "\n";
    if (logOk) log << "Output dir exists: " << QDir(outputPath).exists() << "\n";
    QDir().mkpath(outputPath);
    if (logOk) log << "Output dir after mkpath: " << QDir(outputPath).exists() << "\n\n";

    // --- find ff7.exe --------------------------------------------------------
    QString exePath = findFF7Exe();
    if (exePath.isEmpty()) {
        qDebug() << "ShopRandomizer: ff7 exe not found";
        if (logOk) {
            log << "ERROR: Could not find ff7 executable\n";
            log << "Searched in: " << m_parent->getFF7Path() << "\n";
            // List files in FF7 directory for diagnosis
            QDir ff7Dir(m_parent->getFF7Path());
            log << "Files in FF7 root:\n";
            for (const QString& f : ff7Dir.entryList(QDir::Files))
                log << "  " << f << "\n";
            log << "Subdirectories:\n";
            for (const QString& d : ff7Dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                log << "  " << d << "/\n";
        }
        return false;
    }
    if (logOk) log << "Found EXE: " << exePath << "\n";

    // --- detect the exe build (classic Steam vs 2026 re-release) -------------
    if (!detectExeLayout(exePath, log)) {
        if (logOk) log << "ERROR: unrecognised ff7_en.exe build (shop table not found)\n";
        return false;
    }

    // --- Shop randomization now uses hext patches only - no exe copying needed ---
    if (logOk) log << "Using hext patch method - no exe copying required\n";

    // --- read shops ----------------------------------------------------------
    QVector<ExeShopRecord> shops;
    if (!readShops(exePath, shops)) {
        if (logOk) log << "ERROR: Failed to read shop data from exe\n";
        return false;
    }
    if (logOk) log << "Read " << shops.size() << " shops from exe\n\n";

    // --- Archipelago shop slots (read before randomizing so reserved token
    //     ids are kept out of normal stock) -----------------------------------
    loadApShops(log);

    // --- price tables -> tiered eligible-item pools --------------------------
    if (!readPrices(exePath, log)) {
        if (logOk) log << "ERROR: Failed to read price tables from exe\n";
        return false;
    }
    buildTieredPools(log);

    // --- randomize -----------------------------------------------------------
    int modified = 0;
    for (int i = 0; i < shops.size(); ++i) {
        ExeShopRecord& s = shops[i];
        ExeShopType t = s.shopType;

        // Skip hotels and vegetable (chocobo green) shops
        if (t == ExeShopType::Hotel || t == ExeShopType::Vegetable) {
            if (logOk) log << "Shop " << i << " (" << shopName(i)
                           << "): SKIP (type " << static_cast<int>(t) << ")\n";
            continue;
        }
        if (s.itemCount == 0) {
            if (logOk) log << "Shop " << i << " (" << shopName(i) << "): SKIP (empty)\n";
            continue;
        }

        if (logOk) log << "Shop " << i << " (" << shopName(i)
                       << ") type=" << static_cast<int>(t)
                       << " items=" << s.itemCount << "\n";

        randomizeShop(i, s, log);
        modified++;
        if (logOk) log << "\n";
    }

    if (logOk) log << "\nShops randomized: " << modified << " / " << shops.size() << "\n";

    // --- inject Archipelago shop slots (token items) -------------------------
    applyApShops(shops, log);

    // --- generate hext patch --------------------------------------------------
    if (!generateHextPatch(outputPath, shops)) {
        if (logOk) log << "ERROR: Failed to generate hext patch\n";
        return false;
    }

    if (logOk) {
        log << "SUCCESS: Hext patch generated in hext/ff7/en/\n";
        log << "Session completed: " << QDateTime::currentDateTime().toString() << "\n";
        logFile.close();
    }

    qDebug() << "Shop randomization complete." << modified << "shops modified.";
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Find the FF7 executable
// ─────────────────────────────────────────────────────────────────────────────

QString ShopRandomizer::findFF7Exe() const
{
    QString ff7Path = m_parent->getFF7Path();

    // 1) Check well-known names first
    QStringList candidates = {
        ff7Path + "/ff7_en.exe",         // Steam English
        ff7Path + "/ff7_fr.exe",         // Steam French
        ff7Path + "/ff7_de.exe",         // Steam German
        ff7Path + "/ff7_es.exe",         // Steam Spanish
        ff7Path + "/ff7.exe",            // 1998 PC / generic
    };
    for (const QString& p : candidates) {
        if (QFile::exists(p)) return p;
    }

    // 2) Glob: any exe whose name contains "ff7" (case-insensitive)
    QDir ff7Dir(ff7Path);
    QStringList exeFiles = ff7Dir.entryList(QStringList() << "*.exe", QDir::Files);
    for (const QString& f : exeFiles) {
        if (f.toLower().contains("ff7")) return ff7Dir.filePath(f);
    }

    // 3) Check the output path in case user already copied an exe there
    QString outputPath = m_parent->getOutputPath();
    QDir outDir(outputPath);
    if (outDir.exists()) {
        QStringList outExes = outDir.entryList(QStringList() << "*.exe", QDir::Files);
        for (const QString& f : outExes) {
            if (f.toLower().contains("ff7")) return outDir.filePath(f);
        }
    }

    return QString();
}

// ─────────────────────────────────────────────────────────────────────────────
// Read / write 80 shop records from/to the exe
// ─────────────────────────────────────────────────────────────────────────────

bool ShopRandomizer::detectExeLayout(const QString& exePath, QTextStream& log)
{
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    auto looksLikeShopTable = [&](qint64 pos) -> bool {
        // Probe: the first 10 shop records must all be structurally valid.
        if (!f.seek(pos)) return false;
        const QByteArray raw = f.read(10 * ExeShopRecord::RECORD_BYTES);
        if (raw.size() != 10 * ExeShopRecord::RECORD_BYTES) return false;
        for (int i = 0; i < 10; ++i) {
            const char* d = raw.constData() + i * ExeShopRecord::RECORD_BYTES;
            const quint16 type = static_cast<quint8>(d[0]) | (static_cast<quint8>(d[1]) << 8);
            const quint8  cnt  = static_cast<quint8>(d[2]);
            if (type > 8 || cnt > ExeShopRecord::SLOT_COUNT) return false;
        }
        return true;
    };

    const qint64 candidates[] = { SHOP_POS_CLASSIC, SHOP_POS_2026 };
    for (qint64 c : candidates) {
        if (looksLikeShopTable(c)) {
            m_shopPos         = c;
            m_itemPricePos    = c + ITEM_PRICE_DELTA;
            m_materiaPricePos = c + MATERIA_PRICE_DELTA;
            f.close();
            log << "Exe layout: shop table @0x" << QString::number(c, 16).toUpper()
                << (c == SHOP_POS_2026 ? " (2026 re-release)" : " (classic Steam)")
                << ", prices @0x" << QString::number(m_itemPricePos, 16).toUpper()
                << "/0x" << QString::number(m_materiaPricePos, 16).toUpper() << "\n";
            return true;
        }
    }
    f.close();
    return false;
}

bool ShopRandomizer::readShops(const QString& exePath, QVector<ExeShopRecord>& shops)
{
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "ShopRandomizer: cannot open exe for reading:" << exePath;
        return false;
    }

    if (f.size() < m_shopPos + NUM_SHOPS * ExeShopRecord::RECORD_BYTES) {
        qDebug() << "ShopRandomizer: exe too small – wrong file?";
        f.close();
        return false;
    }

    f.seek(m_shopPos);
    shops.resize(NUM_SHOPS);

    for (int i = 0; i < NUM_SHOPS; ++i) {
        QByteArray raw = f.read(ExeShopRecord::RECORD_BYTES);
        if (raw.size() != ExeShopRecord::RECORD_BYTES) {
            qDebug() << "ShopRandomizer: short read at shop" << i;
            f.close();
            return false;
        }

        const char* d = raw.constData();
        quint16 typeVal;
        std::memcpy(&typeVal, d, 2);
        shops[i].shopType  = static_cast<ExeShopType>(typeVal);
        shops[i].itemCount = static_cast<quint8>(d[2]);
        // d[3] = padding

        for (int s = 0; s < ExeShopRecord::SLOT_COUNT; ++s) {
            const char* slot = d + 4 + s * 8;
            std::memcpy(&shops[i].entries[s].type,    slot,     4);
            std::memcpy(&shops[i].entries[s].index,   slot + 4, 2);
            std::memcpy(&shops[i].entries[s].padding, slot + 6, 2);
        }
    }

    f.close();
    return true;
}

bool ShopRandomizer::generateHextPatch(const QString& outputPath, const QVector<ExeShopRecord>& shops)
{
    // Create hext/ff7/en directory structure
    QString hextDir = QDir(outputPath).filePath("hext/ff7/en");
    QDir().mkpath(hextDir);
    
    QString hextPath = QDir(hextDir).filePath("ff7_shop_randomization.hext");
    QFile hextFile(hextPath);
    if (!hextFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "ShopRandomizer: cannot create hext file:" << hextPath;
        return false;
    }

    QTextStream hext(&hextFile);
    hext << "# FF7 Shop Randomization Hext Patch\n";
    hext << "# Generated by GoldSaucer on " << QDateTime::currentDateTime().toString() << "\n";
    hext << "# Apply with 7th Heaven or other hext-compatible mod loader\n\n";

    // Write each shop as a hext entry
    for (int i = 0; i < NUM_SHOPS && i < shops.size(); ++i) {
        // FFNx Hext patches live memory, so use the virtual address, not the
        // file offset.
        qint64 address = SHOP_INVENTORY_VA + i * ExeShopRecord::RECORD_BYTES;

        // Build the shop record bytes
        QByteArray rec(ExeShopRecord::RECORD_BYTES, '\0');
        char* d = rec.data();

        quint16 typeVal = static_cast<quint16>(shops[i].shopType);
        std::memcpy(d, &typeVal, 2);
        d[2] = static_cast<char>(shops[i].itemCount);
        d[3] = 0; // padding

        for (int s = 0; s < ExeShopRecord::SLOT_COUNT; ++s) {
            char* slot = d + 4 + s * 8;
            std::memcpy(slot,     &shops[i].entries[s].type,    4);
            std::memcpy(slot + 4, &shops[i].entries[s].index,   2);
            std::memcpy(slot + 6, &shops[i].entries[s].padding, 2);
        }

        // Convert to hex string
        QString hexBytes;
        for (char byte : rec) {
            hexBytes += QString("%1 ").arg(static_cast<quint8>(byte), 2, 16, QChar('0')).toUpper();
        }
        hexBytes = hexBytes.trimmed();

        // FFNx Hext line format: "<VA> = <space-separated bytes>".
        // (A leading "+" would be parsed as a global offset, not a patch.)
        hext << QString::number(address, 16).toUpper() << " = " << hexBytes << "\n";
    }

    // AP materia tokens use otherwise-unused materia ids whose price-table entry is
    // typically 0 (free). A free purchase deducts no gil — but the shophook tells a
    // materia BUY from a hover by the gil drop, so a 0-price token can never fire its
    // check. Write a fixed price for each AP materia token so it both sells and costs
    // gil. (Item tokens are real items with real prices, so they need no help.)
    // Materia price table VA = SHOP_INVENTORY_VA + MATERIA_PRICE_DELTA (constant across
    // builds; the file-offset deltas differ but the loaded VA is the same).
    const quint32 AP_MATERIA_TOKEN_PRICE = 100;   // nonzero + affordable anywhere
    for (const ApShopSlot& e : m_apShops) {
        if (!e.isMateria) continue;
        const qint64 priceAddr = SHOP_INVENTORY_VA + MATERIA_PRICE_DELTA
                               + static_cast<qint64>(e.token) * 4;
        QString priceBytes;
        for (int b = 0; b < 4; ++b)
            priceBytes += QString("%1 ").arg((AP_MATERIA_TOKEN_PRICE >> (b * 8)) & 0xFF,
                                             2, 16, QChar('0')).toUpper();
        hext << QString::number(priceAddr, 16).toUpper() << " = " << priceBytes.trimmed() << "\n";
    }

    hextFile.close();
    qDebug() << "ShopRandomizer: Hext patch written to" << hextPath;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Archipelago shop slots
// ─────────────────────────────────────────────────────────────────────────────

void ShopRandomizer::loadApShops(QTextStream& log)
{
    m_apShops.clear();
    m_reservedTokens.clear();
    m_reservedMateria.clear();

    QString apJson = m_parent->m_config.getApJsonPath();
    if (apJson.isEmpty()) {
        log << "AP shops: no apJsonPath configured — skipping AP shop slots\n";
        return;
    }
    QFile f(apJson);
    if (!f.open(QIODevice::ReadOnly)) {
        log << "AP shops: cannot open " << apJson << "\n";
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    const QJsonArray shops = doc.object().value("shops").toArray();
    for (const QJsonValue& v : shops) {
        const QJsonObject o = v.toObject();
        int shopId = o.value("shop_id").toInt(-1);
        int token  = o.value("token_id").toInt(-1);
        const bool isMateria = (o.value("token_type").toString("item") == "materia");
        // item tokens are composite ids (0x000–0x13F); materia tokens are 0x00–0x5A.
        const int maxToken = isMateria ? MATERIA_MAX_ID : (COMPOSITE_COUNT - 1);
        if (shopId < 0 || token < 0 || token > maxToken)
            continue;
        m_apShops.append({ shopId, static_cast<quint16>(token), isMateria });
        if (isMateria) m_reservedMateria.insert(static_cast<quint16>(token));
        else           m_reservedTokens.insert(static_cast<quint16>(token));
    }
    log << "AP shops: " << m_apShops.size() << " slot(s), "
        << m_reservedTokens.size() << " item + " << m_reservedMateria.size()
        << " materia reserved token id(s)\n";
}

void ShopRandomizer::applyApShops(QVector<ExeShopRecord>& shops, QTextStream& log)
{
    for (const ApShopSlot& e : m_apShops) {
        const int     shopId = e.shopId;
        const quint16 token  = e.token;
        if (shopId < 0 || shopId >= shops.size()) {
            log << "AP shop: skip bad shop_id " << shopId << "\n";
            continue;
        }
        ExeShopRecord& s = shops[shopId];
        // Append the AP token as a new slot so normal stock is preserved; if the
        // shop is already full (10 slots), overwrite the last slot.
        int slot = s.itemCount;
        if (slot >= ExeShopRecord::SLOT_COUNT)
            slot = ExeShopRecord::SLOT_COUNT - 1;
        s.entries[slot].type    = e.isMateria ? 1 : 0;  // 1 = materia, 0 = item/weapon/etc.
        s.entries[slot].index   = token;
        s.entries[slot].padding = 0;
        if (s.itemCount < ExeShopRecord::SLOT_COUNT)
            s.itemCount = static_cast<quint8>(slot + 1);
        log << "AP shop " << shopId << " (" << shopName(shopId) << "): "
            << (e.isMateria ? "materia" : "item") << " token 0x"
            << QString::number(token, 16) << " at slot " << slot << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-shop randomization (category-aware)
// ─────────────────────────────────────────────────────────────────────────────

void ShopRandomizer::randomizeShop(int shopId, ExeShopRecord& shop, QTextStream& log)
{
    const int tier = shopTier(shopId);
    for (int i = 0; i < shop.itemCount && i < ExeShopRecord::SLOT_COUNT; ++i) {
        ExeShopSlot& entry = shop.entries[i];
        quint16 oldIndex = entry.index;
        qint32  oldType  = entry.type;

        if (shop.shopType == ExeShopType::Materia) {
            // Materia shop – keep type=1, pick a price-appropriate materia
            entry.type  = 1;
            entry.index = pickTiered(CatMateria, tier);
        } else {
            // Non-materia shop – pick from the appropriate category at this tier
            entry.type  = 0;
            entry.index = randomFromCategory(shop.shopType, tier);
        }
        entry.padding = 0;

        log << "  [" << i << "] type " << oldType << " idx " << oldIndex
            << " -> type " << entry.type << " idx " << entry.index
            << " (tier " << tier << ")\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Price tables -> tiered eligible-item pools
// ─────────────────────────────────────────────────────────────────────────────

bool ShopRandomizer::readPrices(const QString& exePath, QTextStream& log)
{
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly)) {
        log << "readPrices: cannot open " << exePath << "\n";
        return false;
    }
    m_itemPrices.resize(COMPOSITE_COUNT);
    m_materiaPrices.resize(MATERIA_MAX_ID + 1);

    f.seek(m_itemPricePos);
    QByteArray ib = f.read(static_cast<qint64>(COMPOSITE_COUNT) * 4);
    f.seek(m_materiaPricePos);
    QByteArray mb = f.read(static_cast<qint64>(MATERIA_MAX_ID + 1) * 4);
    f.close();

    if (ib.size() != COMPOSITE_COUNT * 4 || mb.size() != (MATERIA_MAX_ID + 1) * 4) {
        log << "readPrices: short read (item " << ib.size()
            << " materia " << mb.size() << ")\n";
        return false;
    }
    for (int i = 0; i < COMPOSITE_COUNT; ++i)
        std::memcpy(&m_itemPrices[i], ib.constData() + i * 4, 4);
    for (int i = 0; i <= MATERIA_MAX_ID; ++i)
        std::memcpy(&m_materiaPrices[i], mb.constData() + i * 4, 4);

    log << "readPrices: loaded " << COMPOSITE_COUNT << " item + "
        << (MATERIA_MAX_ID + 1) << " materia prices\n";
    return true;
}

void ShopRandomizer::buildTieredPools(QTextStream& log)
{
    // Materia ids with no real entry (would render as broken materia).
    static const QSet<quint16> kMateriaGaps = {
        0x16, 0x26, 0x2D, 0x2E, 0x2F, 0x3F, 0x42, 0x43
    };

    auto split = [&](int cat, QVector<QPair<quint32, quint16>>& priced) {
        std::sort(priced.begin(), priced.end(),
                  [](const QPair<quint32, quint16>& a, const QPair<quint32, quint16>& b) {
                      return a.first < b.first;  // ascending by price
                  });
        const int n = priced.size();
        for (int t = 0; t < NUM_TIERS; ++t) {
            m_pool[cat][t].clear();
            const int lo = n * t / NUM_TIERS;
            const int hi = n * (t + 1) / NUM_TIERS;
            for (int k = lo; k < hi; ++k)
                m_pool[cat][t].append(priced[k].second);
        }
        log << "  pool cat " << cat << ": " << n << " sellable -> tiers ["
            << m_pool[cat][0].size() << "," << m_pool[cat][1].size() << ","
            << m_pool[cat][2].size() << "]\n";
    };

    // Composite categories (items/weapons/armor/accessories) share m_itemPrices.
    struct CatRange { int cat; int start; int end; };  // inclusive composite ids
    const CatRange comp[] = {
        { CatItem,      0x00,            ITEM_COUNT - 1 },
        { CatWeapon,    WEAPON_START,    WEAPON_START + WEAPON_COUNT - 1 },
        { CatArmor,     ARMOR_START,     ARMOR_START + ARMOR_COUNT - 1 },
        { CatAccessory, ACCESSORY_START, ACCESSORY_START + ACCESSORY_COUNT - 1 },
    };
    for (const CatRange& cr : comp) {
        QVector<QPair<quint32, quint16>> priced;
        for (int id = cr.start; id <= cr.end && id < COMPOSITE_COUNT; ++id) {
            const quint32 price = m_itemPrices[id];
            if (price < SELLABLE_MIN) continue;                       // unsellable sentinel
            if (m_reservedTokens.contains(static_cast<quint16>(id))) continue; // AP token id
            priced.append(qMakePair(price, static_cast<quint16>(id)));
        }
        split(cr.cat, priced);
    }

    // Materia (own price table); skip gaps + unsellable (Enemy Skill, KOTR, Masters).
    {
        QVector<QPair<quint32, quint16>> priced;
        for (int id = 0; id <= MATERIA_MAX_ID; ++id) {
            if (kMateriaGaps.contains(static_cast<quint16>(id))) continue;
            if (m_reservedMateria.contains(static_cast<quint16>(id))) continue; // AP token
            const quint32 price = m_materiaPrices[id];
            if (price < SELLABLE_MIN) continue;
            priced.append(qMakePair(price, static_cast<quint16>(id)));
        }
        split(CatMateria, priced);
    }
}

int ShopRandomizer::shopTier(int id) const
{
    // World-progression tiers (see shopName()): 0 = early, 1 = mid, 2 = late.
    if (id <= 25) return 0;                                 // Midgar, Kalm, Fort Condor D1, Junon, Cargo
    if (id >= 51 && id <= 59) return 0;                     // Fort Condor D2, Junon D2
    if ((id >= 26 && id <= 42) || (id >= 60 && id <= 64))  // Costa..Rocket Town (+disc-2 copies)
        return 1;
    return 2;                                               // Wutai, Temple, Icicle, Mideel, Bone Village
}

quint16 ShopRandomizer::pickTiered(int category, int tier) const
{
    if (category < 0 || category >= CatCOUNT)
        return 0;
    // Prefer the requested tier; fall back to the nearest non-empty tier so a
    // sparse category (e.g. armor) never leaves a slot unfilled.
    for (int d = 0; d < NUM_TIERS; ++d) {
        for (int t : { tier + d, tier - d }) {
            if (t < 0 || t >= NUM_TIERS) continue;
            const QVector<quint16>& pool = m_pool[category][t];
            if (!pool.isEmpty()) {
                const int idx = std::uniform_int_distribution<int>(0, pool.size() - 1)(
                    const_cast<std::mt19937&>(m_rng));
                return pool[idx];
            }
        }
    }
    return (category == CatMateria) ? 0x35 : 0x00;  // Restore / Potion (safe defaults)
}

quint16 ShopRandomizer::randomFromCategory(ExeShopType shopType, int tier) const
{
    switch (shopType) {
    case ExeShopType::Weapon:
        return pickTiered(CatWeapon, tier);
    case ExeShopType::Accessory:
        return pickTiered(CatAccessory, tier);
    case ExeShopType::Materia:
        return pickTiered(CatMateria, tier);   // shouldn't reach here but just in case
    case ExeShopType::General:
    case ExeShopType::Tool:
        // Mixed shop – any non-materia category (CatItem..CatAccessory = 0..3).
        return pickTiered(
            std::uniform_int_distribution<int>(CatItem, CatAccessory)(
                const_cast<std::mt19937&>(m_rng)),
            tier);
    case ExeShopType::Item:
    case ExeShopType::Item2:
    default:
        return pickTiered(CatItem, tier);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shop name table (for debug log readability)
// ─────────────────────────────────────────────────────────────────────────────

QString ShopRandomizer::shopName(int id)
{
    static const char* names[] = {
        /*  0 */ "Sector 7 Weapon Shop",
        /*  1 */ "Sector 7 Item Shop",
        /*  2 */ "Sector 7 Drug Store",
        /*  3 */ "Sector 8 Weapon Shop",
        /*  4 */ "Sector 8 Item Shop",
        /*  5 */ "Sector 8 Materia Shop",
        /*  6 */ "Wall Market Weapon Shop",
        /*  7 */ "Wall Market Materia Shop",
        /*  8 */ "Wall Market Item Shop",
        /*  9 */ "Sector 7 Pillar Shop",
        /* 10 */ "Shinra HQ Shop",
        /* 11 */ "Kalm Weapon Shop",
        /* 12 */ "Kalm Item Shop",
        /* 13 */ "Kalm Materia Shop",
        /* 14 */ "Choco Billy Greens (D1)",
        /* 15 */ "Choco Billy Greens (D2)",
        /* 16 */ "Fort Condor Item (D1)",
        /* 17 */ "Fort Condor Materia (D1)",
        /* 18 */ "Lower Junon Weapon",
        /* 19 */ "Upper Junon Weapon #1 (D1)",
        /* 20 */ "Upper Junon Item (D1)",
        /* 21 */ "Upper Junon Materia #1",
        /* 22 */ "Upper Junon Weapon #2 (D1)",
        /* 23 */ "Upper Junon Accessory (D1)",
        /* 24 */ "Upper Junon Materia #2 (D1)",
        /* 25 */ "Cargo Ship Item",
        /* 26 */ "Costa Del Sol Weapon (D1)",
        /* 27 */ "Costa Del Sol Materia (D1)",
        /* 28 */ "Costa Del Sol Item (D1)",
        /* 29 */ "North Corel Weapon",
        /* 30 */ "North Corel Item",
        /* 31 */ "North Corel General",
        /* 32 */ "Gold Saucer Hotel",
        /* 33 */ "Corel Prison General",
        /* 34 */ "Gongaga Weapon",
        /* 35 */ "Gongaga Item",
        /* 36 */ "Gongaga Accessory",
        /* 37 */ "Cosmo Canyon Weapon",
        /* 38 */ "Cosmo Canyon Item",
        /* 39 */ "Cosmo Canyon Materia",
        /* 40 */ "Nibelheim General",
        /* 41 */ "Rocket Town Weapon (D1)",
        /* 42 */ "Rocket Town Item (D1)",
        /* 43 */ "Wutai Weapon",
        /* 44 */ "Wutai Item",
        /* 45 */ "Temple of Ancients",
        /* 46 */ "Icicle Inn Weapon",
        /* 47 */ "Mideel Weapon",
        /* 48 */ "Mideel Accessory",
        /* 49 */ "Mideel Item",
        /* 50 */ "Mideel Materia",
        /* 51 */ "Fort Condor Item (D2)",
        /* 52 */ "Fort Condor Materia (D2)",
        /* 53 */ "Chocobo Sage Greens",
        /* 54 */ "Upper Junon Weapon #1 (D2)",
        /* 55 */ "Upper Junon Item (D2)",
        /* 56 */ "Shop 56",
        /* 57 */ "Upper Junon Weapon #2 (D2)",
        /* 58 */ "Upper Junon Accessory (D2)",
        /* 59 */ "Upper Junon Materia #2 (D2)",
        /* 60 */ "Costa Del Sol Weapon (D2)",
        /* 61 */ "Costa Del Sol Materia (D2)",
        /* 62 */ "Costa Del Sol Item (D2)",
        /* 63 */ "Rocket Town Weapon (D2)",
        /* 64 */ "Rocket Town Item (D2)",
        /* 65 */ "Bone Village",
    };
    const int count = sizeof(names) / sizeof(names[0]);
    if (id >= 0 && id < count) return QString::fromLatin1(names[id]);
    return QString("Shop %1").arg(id);
}
