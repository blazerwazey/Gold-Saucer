#include "ShopRandomizer.h"
#include "Randomizer.h"
#include "Config.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QDateTime>
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

    // --- Shop randomization now uses hext patches only - no exe copying needed ---
    if (logOk) log << "Using hext patch method - no exe copying required\n";

    // --- read shops ----------------------------------------------------------
    QVector<ExeShopRecord> shops;
    if (!readShops(exePath, shops)) {
        if (logOk) log << "ERROR: Failed to read shop data from exe\n";
        return false;
    }
    if (logOk) log << "Read " << shops.size() << " shops from exe\n\n";

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

        randomizeShop(s, log);
        modified++;
        if (logOk) log << "\n";
    }

    if (logOk) log << "\nShops randomized: " << modified << " / " << shops.size() << "\n";

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

bool ShopRandomizer::readShops(const QString& exePath, QVector<ExeShopRecord>& shops)
{
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "ShopRandomizer: cannot open exe for reading:" << exePath;
        return false;
    }

    if (f.size() < SHOP_INVENTORY_POS + NUM_SHOPS * ExeShopRecord::RECORD_BYTES) {
        qDebug() << "ShopRandomizer: exe too small – wrong file?";
        f.close();
        return false;
    }

    f.seek(SHOP_INVENTORY_POS);
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
        qint64 address = SHOP_INVENTORY_POS + i * ExeShopRecord::RECORD_BYTES;
        
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

        hext << "+0x" << QString::number(address, 16).toUpper() << " " << hexBytes << "\n";
    }

    hextFile.close();
    qDebug() << "ShopRandomizer: Hext patch written to" << hextPath;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-shop randomization (category-aware)
// ─────────────────────────────────────────────────────────────────────────────

void ShopRandomizer::randomizeShop(ExeShopRecord& shop, QTextStream& log)
{
    for (int i = 0; i < shop.itemCount && i < ExeShopRecord::SLOT_COUNT; ++i) {
        ExeShopSlot& entry = shop.entries[i];
        quint16 oldIndex = entry.index;
        qint32  oldType  = entry.type;

        if (shop.shopType == ExeShopType::Materia) {
            // Materia shop – keep type=1, randomize materia ID
            entry.type  = 1;
            entry.index = randomMateria();
        } else {
            // Non-materia shop – pick from the appropriate category
            entry.type  = 0;
            entry.index = randomFromCategory(shop.shopType);
        }
        entry.padding = 0;

        log << "  [" << i << "] type " << oldType << " idx " << oldIndex
            << " -> type " << entry.type << " idx " << entry.index << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Random item generators
// ─────────────────────────────────────────────────────────────────────────────

quint16 ShopRandomizer::randomItem() const
{
    // Consumable items 0x00–0x68  (105 items)
    return static_cast<quint16>(
        std::uniform_int_distribution<int>(0, ITEM_COUNT - 1)(
            const_cast<std::mt19937&>(m_rng)));
}

quint16 ShopRandomizer::randomWeapon() const
{
    // Weapons: composite index 0x80 + (0..127)
    return WEAPON_START + static_cast<quint16>(
        std::uniform_int_distribution<int>(0, WEAPON_COUNT - 1)(
            const_cast<std::mt19937&>(m_rng)));
}

quint16 ShopRandomizer::randomArmor() const
{
    return ARMOR_START + static_cast<quint16>(
        std::uniform_int_distribution<int>(0, ARMOR_COUNT - 1)(
            const_cast<std::mt19937&>(m_rng)));
}

quint16 ShopRandomizer::randomAccessory() const
{
    return ACCESSORY_START + static_cast<quint16>(
        std::uniform_int_distribution<int>(0, ACCESSORY_COUNT - 1)(
            const_cast<std::mt19937&>(m_rng)));
}

quint16 ShopRandomizer::randomMateria() const
{
    return static_cast<quint16>(
        std::uniform_int_distribution<int>(0, MATERIA_COUNT - 1)(
            const_cast<std::mt19937&>(m_rng)));
}

quint16 ShopRandomizer::randomFromCategory(ExeShopType shopType) const
{
    switch (shopType) {
    case ExeShopType::Weapon:
        return randomWeapon();
    case ExeShopType::Accessory:
        return randomAccessory();
    case ExeShopType::Materia:
        return randomMateria();   // shouldn't reach here but just in case
    case ExeShopType::General:
    case ExeShopType::Tool: {
        // Mixed shop – pick from any non-materia category
        int roll = std::uniform_int_distribution<int>(0, 3)(
            const_cast<std::mt19937&>(m_rng));
        switch (roll) {
        case 0:  return randomItem();
        case 1:  return randomWeapon();
        case 2:  return randomArmor();
        default: return randomAccessory();
        }
    }
    case ExeShopType::Item:
    case ExeShopType::Item2:
    default:
        return randomItem();
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
