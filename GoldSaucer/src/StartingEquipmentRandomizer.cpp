#include "StartingEquipmentRandomizer.h"
#include "TextReplacementConfig.h"
#include "TextEncoder.h"
#include "Randomizer.h"
#include "Config.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QByteArray>
#include <QTextStream>
#include <algorithm>
#include <ff7tk/data/FF7Char.h>
#include <ff7tk/utils/GZIP.h>
#include <zlib.h>

// Decompress one gzip stream starting at `offset` in `data`.
// Returns decompressed bytes and sets `compressedSize` to the number of
// input bytes consumed (so the caller can advance to the next section).
static QByteArray inflateGzipSection(const QByteArray& data, int offset, int& compressedSize)
{
    compressedSize = 0;
    if (offset >= data.size()) return QByteArray();

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    // 15 + 16 tells zlib to expect a gzip header
    if (inflateInit2(&strm, 15 + 16) != Z_OK) return QByteArray();

    uInt totalAvail = static_cast<uInt>(data.size() - offset);
    strm.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData() + offset));
    strm.avail_in = totalAvail;

    QByteArray out;
    char buf[8192];
    int ret;
    do {
        strm.next_out  = reinterpret_cast<Bytef*>(buf);
        strm.avail_out = sizeof(buf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            return QByteArray();
        }
        out.append(buf, static_cast<int>(sizeof(buf) - strm.avail_out));
    } while (ret != Z_STREAM_END);

    // total_in only counts deflate bytes; use avail_in delta to include
    // the full gzip envelope (header + compressed data + 8-byte trailer)
    compressedSize = static_cast<int>(totalAvail - strm.avail_in);
    inflateEnd(&strm);
    return out;
}

// Log file for debugging kernel.bin operations
static QFile* g_logFile = nullptr;
static QTextStream* g_logStream = nullptr;

static void initLog(const QString& outputPath) {
    if (g_logFile) return;
    QString logPath = QDir(outputPath).filePath("kernel_debug.log");
    g_logFile = new QFile(logPath);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        g_logStream = new QTextStream(g_logFile);
    }
}

static void log(const QString& msg) {
    qDebug() << msg;
    if (g_logStream) {
        *g_logStream << msg << "\n";
        g_logStream->flush();
    }
}

static void closeLog() {
    if (g_logStream) {
        delete g_logStream;
        g_logStream = nullptr;
    }
    if (g_logFile) {
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}

// kernel.bin format: 27 GZIP-compressed sections
// Each section has 6-byte header: 2 bytes compressed size, 2 bytes decompressed size, 2 bytes file type
// Section 4 (index 3) contains initialization/starting equipment data

// FF7CHAR structure (from ff7tk Type_FF7CHAR.h):
// - Total size: 132 bytes per character (9 characters in init data)
// - weapon at offset 0x001C (28) - 1 byte
// - armor at offset 0x001D (29) - 1 byte
// - accessory at offset 0x001E (30) - 1 byte
// Item indexes: 0-127 items, 128-255 weapons, 256-287 armor, 288-319 accessories

StartingEquipmentRandomizer::StartingEquipmentRandomizer(Randomizer* parent)
    : m_parent(parent)
    , m_rng(const_cast<std::mt19937&>(parent->m_rng))
{
    initializeEquipmentPools();
}

// Use FF7tk's GZIP class for proper decompression/compression

bool StartingEquipmentRandomizer::randomize()
{
    QString outputPath = m_parent->getOutputPath();
    QDir().mkpath(outputPath);
    initLog(outputPath);
    log("=== Starting Equipment Randomization ===");

    // --- find kernel.bin -----------------------------------------------------
    QString kernelPath = findKernelBin();
    if (kernelPath.isEmpty()) {
        log("ERROR: Could not find kernel.bin");
        closeLog();
        return false;
    }
    log("Found kernel.bin: " + kernelPath);

    // --- copy to output if not already there ---------------------------------
    QString kernelName = QFileInfo(kernelPath).fileName();
    // Preserve the relative directory structure (e.g. data/lang-en/kernel/)
    QString relPath = kernelPath.mid(m_parent->getFF7Path().length());
    QString outKernel = outputPath + relPath;
    QDir().mkpath(QFileInfo(outKernel).path());
    // Always start from a fresh copy of the original kernel.bin
    if (QFile::exists(outKernel))
        QFile::remove(outKernel);
    if (!QFile::copy(kernelPath, outKernel)) {
        log("ERROR: Failed to copy kernel.bin to output");
        closeLog();
        return false;
    }
    QFile::setPermissions(outKernel, QFile::ReadOwner | QFile::WriteOwner
                                    | QFile::ReadGroup | QFile::ReadOther);
    log("Working on: " + outKernel);

    // --- read the whole file -------------------------------------------------
    QFile f(outKernel);
    if (!f.open(QIODevice::ReadOnly)) {
        log("ERROR: Cannot open kernel.bin for reading");
        closeLog();
        return false;
    }
    QByteArray raw = f.readAll();
    f.close();
    log("kernel.bin size: " + QString::number(raw.size()) + " bytes");

    // --- parse kernel.bin section table --------------------------------------
    // FF7 PC kernel.bin format: 9 sections, each preceded by a 6-byte header:
    //   uint16 compressedSize (LE)
    //   uint16 decompressedSize (LE)
    //   uint16 sectionType (LE)
    //   [compressedSize bytes of gzip data]
    const int SECTION_HEADER_SIZE = 6;
    struct KSection { int offset; quint16 compSize; quint16 decSize; };
    QVector<KSection> sections;
    int pos = 0;
    while (pos + SECTION_HEADER_SIZE <= raw.size() && sections.size() < 9) {
        quint16 compSize, decSize, secType;
        memcpy(&compSize, raw.constData() + pos, 2);
        memcpy(&decSize,  raw.constData() + pos + 2, 2);
        memcpy(&secType,  raw.constData() + pos + 4, 2);
        if (pos + SECTION_HEADER_SIZE + compSize > raw.size()) break;
        KSection ks;
        ks.offset   = pos;
        ks.compSize = compSize;
        ks.decSize  = decSize;
        sections.append(ks);
        log("  section " + QString::number(sections.size() - 1)
            + ": offset=" + QString::number(pos)
            + " compressed=" + QString::number(compSize)
            + " decompressed=" + QString::number(decSize)
            + " type=" + QString::number(secType));
        pos += SECTION_HEADER_SIZE + compSize;
    }
    log("Parsed " + QString::number(sections.size()) + " sections");

    if (sections.size() < 4) {
        log("ERROR: Not enough sections in kernel.bin (need at least 4, found "
            + QString::number(sections.size()) + ")");
        closeLog();
        return false;
    }

    // --- decompress section 3 (Initialization Data) --------------------------
    const KSection& sec3 = sections[3];
    int sec3GzipOff = sec3.offset + SECTION_HEADER_SIZE;
    QByteArray sec3Gzip = raw.mid(sec3GzipOff, sec3.compSize);
    QByteArray initData = GZIP::decompress(sec3Gzip, sec3.decSize);
    if (initData.isEmpty()) {
        log("ERROR: Failed to decompress section 3");
        closeLog();
        return false;
    }
    log("Section 3 decompressed: " + QString::number(initData.size()) + " bytes");

    // --- randomize character equipment ---------------------------------------
    randomizeStartingEquipment(initData);

    // --- recompress section 3 ------------------------------------------------
    QByteArray sec3Recompressed = GZIP::compress(initData);
    if (sec3Recompressed.isEmpty()) {
        log("ERROR: Failed to recompress section 3");
        closeLog();
        return false;
    }
    log("Section 3 recompressed: " + QString::number(sec3Recompressed.size()) + " bytes");

    // --- rebuild kernel.bin --------------------------------------------------
    // Update the 6-byte header for section 3 with the new compressed size,
    // then reassemble all sections.
    QByteArray rebuilt;
    for (int i = 0; i < sections.size(); ++i) {
        if (i == 3) {
            // Write updated header
            quint16 newCompSize = static_cast<quint16>(sec3Recompressed.size());
            quint16 newDecSize  = static_cast<quint16>(initData.size());
            quint16 secType;
            memcpy(&secType, raw.constData() + sec3.offset + 4, 2);
            rebuilt.append(reinterpret_cast<const char*>(&newCompSize), 2);
            rebuilt.append(reinterpret_cast<const char*>(&newDecSize), 2);
            rebuilt.append(reinterpret_cast<const char*>(&secType), 2);
            rebuilt.append(sec3Recompressed);
        } else {
            // Copy original header + gzip data unchanged
            int totalSize = SECTION_HEADER_SIZE + sections[i].compSize;
            rebuilt.append(raw.mid(sections[i].offset, totalSize));
        }
    }
    // Append any trailing data after the last parsed section
    int lastEnd = sections.last().offset + SECTION_HEADER_SIZE + sections.last().compSize;
    if (lastEnd < raw.size())
        rebuilt.append(raw.mid(lastEnd));

    // --- write back ----------------------------------------------------------
    QFile out(outKernel);
    if (!out.open(QIODevice::WriteOnly)) {
        log("ERROR: Cannot open kernel.bin for writing");
        closeLog();
        return false;
    }
    out.write(rebuilt);
    out.close();

    log("SUCCESS: kernel.bin written (" + QString::number(rebuilt.size()) + " bytes)");
    closeLog();
    return true;
}

QString StartingEquipmentRandomizer::findKernelBin() const
{
    QString ff7Path = m_parent->getFF7Path();
    QStringList candidates = {
        ff7Path + "/data/lang-en/kernel/kernel.bin",   // Steam English
        ff7Path + "/data/lang-fr/kernel/kernel.bin",   // Steam French
        ff7Path + "/data/lang-de/kernel/kernel.bin",   // Steam German
        ff7Path + "/data/lang-es/kernel/kernel.bin",   // Steam Spanish
        ff7Path + "/data/kernel.bin",                   // 1998 PC
        ff7Path + "/kernel.bin",                        // fallback
    };
    for (const QString& p : candidates) {
        if (QFile::exists(p)) return p;
    }
    return QString();
}

bool StartingEquipmentRandomizer::randomizeAll()
{
    return randomize();
}

void StartingEquipmentRandomizer::copyOriginal(const QString& src, const QString& dst)
{
    Q_UNUSED(src)
    Q_UNUSED(dst)
}

bool StartingEquipmentRandomizer::loadInitialData(const QString& filePath, QByteArray& data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Error: Could not open kernel.bin for reading:" << filePath;
        return false;
    }
    
    data = file.readAll();
    file.close();
    
    if (data.isEmpty()) {
        qDebug() << "Error: kernel.bin is empty";
        return false;
    }
    
    return true;
}

bool StartingEquipmentRandomizer::saveInitialData(const QString& filePath, const QByteArray& data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Error: Could not open kernel.bin for writing:" << filePath;
        return false;
    }
    
    qint64 bytesWritten = file.write(data);
    file.close();
    
    if (bytesWritten != data.size()) {
        qDebug() << "Error: Could not write all data to kernel.bin";
        return false;
    }
    
    return true;
}

void StartingEquipmentRandomizer::randomizeStartingEquipment(QByteArray& data)
{
    // Section 4 contains initialization data copied to savemap on New Game
    // Character records are 132 bytes each, starting at the beginning of section 4
    // Characters: Cloud(0), Barret(1), Tifa(2), Aerith(3), Red(4), Yuffie(5), CaitSith(6), Vincent(7), Cid(8)
    
    const int CHAR_RECORD_SIZE = 132;
    const int WEAPON_OFFSET = 0x1C;    // 28
    const int ARMOR_OFFSET = 0x1D;     // 29
    const int ACCESSORY_OFFSET = 0x1E; // 30
    const int MATERIA_OFFSET = 0x40;   // 64 — 16 slots × 4 bytes (id + 3 bytes AP)
    const int MATERIA_SLOT_SIZE = 4;
    const int TOTAL_MATERIA_SLOTS = 16;
    const int MAX_WEAPON_MATERIA = 3;    // cap to avoid exceeding actual weapon slots
    const int MAX_ARMOR_MATERIA  = 2;    // cap to avoid exceeding actual armor slots

    // Valid materia IDs from ff7tk FF7Materia enum (excludes gap/nameless IDs
    // 0x16, 0x26, 0x2D-0x2F, 0x3F, 0x42-0x43 and master materia 0x30, 0x49, 0x5A)
    static const quint8 VALID_MATERIA[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // MP+,HP+,Spd+,Mag+,Lck+,EXP+,Gil+,EnemyAway
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, // EnemyLure,ChocoLure,Pre-empt,LongRange,MegaAll,CounterAtk,SlashAll,DblCut
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,             // Cover,Underwater,HP<>MP,W-Magic,W-Summon,W-Item
        0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, // All,Counter,MagCounter,MPTurbo,MPAbsorb,HPAbsorb,Elemental,AddedEffect
        0x1F, 0x20, 0x21, 0x22, 0x23,                   // SneakAtk,FinalAtk,AddedCut,StealAsWell,QuadraMagic
        0x24, 0x25, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, // Steal,Sense,Throw,Morph,Deathblow,Manipulate,Mime,EnemySkill
        0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, // Fire,Ice,Earth,Lightning,Restore,Heal,Revive,Seal
        0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,             // Mystify,Transform,Exit,Poison,Demi,Barrier
        0x40, 0x41, 0x44, 0x45, 0x46, 0x47, 0x48,       // Comet,Time,Destruct,Contain,FullCure,Shield,Ultima
        0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,             // ChocoMog,Shiva,Ifrit,Ramuh,Titan,Odin
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, // Leviathan,Bahamut,Kujata,Alexander,Phoenix,NeoBahamut,Hades,Typhoon
        0x58, 0x59                                       // BahamutZERO,KOTR
    };
    const int VALID_MATERIA_COUNT = sizeof(VALID_MATERIA) / sizeof(VALID_MATERIA[0]);
    
    qDebug() << "Starting equipment randomization on section 4, size:" << data.size() << "bytes";
    
    // Randomize equipment for all playable characters (excluding Young Cloud and Sephiroth)
    // Characters: Cloud(0), Barret(1), Tifa(2), Aerith(3), Red(4), Yuffie(5), CaitSith(6), Vincent(7), Cid(8)
    QList<int> charactersToRandomize = {
        FF7Char::Cloud,
        FF7Char::Barret, 
        FF7Char::Tifa,
        FF7Char::Aerith,
        FF7Char::Red,
        FF7Char::Yuffie,
        FF7Char::CaitSith,
        FF7Char::Vincent,
        FF7Char::Cid
    };
    
    for (int charId : charactersToRandomize) {
        int charOffset = charId * CHAR_RECORD_SIZE;
        
        if (charOffset + CHAR_RECORD_SIZE > data.size()) {
            qDebug() << "Character" << charId << "offset out of bounds, skipping";
            continue;
        }
        
        // Get valid weapon range for this character
        // Note: FF7Char returns different ranges than kernel.bin storage
        // Kernel.bin stores: Cloud(0-15), Barret(32-47), Tifa(16-31), Aerith(48-63), Red(64-79), Yuffie(80-95), CaitSith(96-111), Vincent(112-127), Cid(128-143)
        int weaponStart, numWeapons;
        switch(charId) {
            case 0: weaponStart = 0; numWeapons = 16; break;   // Cloud: 0-15
            case 1: weaponStart = 32; numWeapons = 16; break;  // Barret: 32-47
            case 2: weaponStart = 16; numWeapons = 16; break;  // Tifa: 16-31
            case 3: weaponStart = 48; numWeapons = 16; break;  // Aerith: 48-63
            case 4: weaponStart = 64; numWeapons = 16; break;  // Red: 64-79
            case 5: weaponStart = 80; numWeapons = 16; break;  // Yuffie: 80-95
            case 6: weaponStart = 96; numWeapons = 16; break;  // CaitSith: 96-111
            case 7: weaponStart = 112; numWeapons = 16; break; // Vincent: 112-127
            case 8: weaponStart = 128; numWeapons = 16; break; // Cid: 128-143
            default: weaponStart = 0; numWeapons = 1; break;   // Default
        }
        
        log(QString("Character %1: weaponStart=%2 numWeapons=%3")
            .arg(charId).arg(weaponStart).arg(numWeapons));
        
        // Randomize weapon (pick from character's valid weapons)
        std::uniform_int_distribution<int> weaponDist(0, numWeapons - 1);
        quint8 newWeapon = static_cast<quint8>(weaponStart + weaponDist(m_rng));
        data[charOffset + WEAPON_OFFSET] = static_cast<char>(newWeapon);
        
        // Randomize armor (0-31 for armor IDs, game adds 256 internally)
        std::uniform_int_distribution<int> armorDist(0, 31);
        quint8 newArmor = static_cast<quint8>(armorDist(m_rng));
        data[charOffset + ARMOR_OFFSET] = static_cast<char>(newArmor);
        
        // Randomize accessory (0-31 for accessory IDs, or 255 for none)
        // 20% chance of no accessory
        std::uniform_real_distribution<double> chanceDist(0.0, 1.0);
        quint8 newAccessory;
        if (chanceDist(m_rng) < 0.2) {
            newAccessory = 255; // No accessory
        } else {
            std::uniform_int_distribution<int> accessoryDist(0, 31);
            newAccessory = static_cast<quint8>(accessoryDist(m_rng));
        }
        data[charOffset + ACCESSORY_OFFSET] = static_cast<char>(newAccessory);
        
        // Randomize materia slots (0-7 weapon, 8-15 armor)
        // Capped to MAX_WEAPON_MATERIA / MAX_ARMOR_MATERIA to stay within slot limits
        std::uniform_int_distribution<int> materiaDist(0, VALID_MATERIA_COUNT - 1);
        QStringList materiaLog;
        int weaponMateriaCount = 0;
        int armorMateriaCount  = 0;
        for (int slot = 0; slot < TOTAL_MATERIA_SLOTS; ++slot) {
            int slotOffset = charOffset + MATERIA_OFFSET + (slot * MATERIA_SLOT_SIZE);
            if (slotOffset + MATERIA_SLOT_SIZE > data.size()) break;

            bool isWeaponSlot = (slot < 8);
            // Enforce slot caps
            if (isWeaponSlot && weaponMateriaCount >= MAX_WEAPON_MATERIA) {
                // Clear remaining weapon slots
                data[slotOffset]     = static_cast<char>(0xFF);
                data[slotOffset + 1] = static_cast<char>(0xFF);
                data[slotOffset + 2] = static_cast<char>(0xFF);
                data[slotOffset + 3] = static_cast<char>(0xFF);
                continue;
            }
            if (!isWeaponSlot && armorMateriaCount >= MAX_ARMOR_MATERIA) {
                data[slotOffset]     = static_cast<char>(0xFF);
                data[slotOffset + 1] = static_cast<char>(0xFF);
                data[slotOffset + 2] = static_cast<char>(0xFF);
                data[slotOffset + 3] = static_cast<char>(0xFF);
                continue;
            }

            double fillChance = isWeaponSlot ? 0.60 : 0.50;
            if (chanceDist(m_rng) < fillChance) {
                quint8 matId = VALID_MATERIA[materiaDist(m_rng)];
                if (isWeaponSlot) ++weaponMateriaCount;
                else ++armorMateriaCount;
                data[slotOffset]     = static_cast<char>(matId);
                data[slotOffset + 1] = 0; // AP byte 0
                data[slotOffset + 2] = 0; // AP byte 1
                data[slotOffset + 3] = 0; // AP byte 2
                materiaLog.append(QString("%1:%2").arg(slot).arg(matId, 0, 16));
            } else {
                data[slotOffset]     = static_cast<char>(0xFF); // empty
                data[slotOffset + 1] = static_cast<char>(0xFF);
                data[slotOffset + 2] = static_cast<char>(0xFF);
                data[slotOffset + 3] = static_cast<char>(0xFF);
            }
        }

        log(QString("Character %1 (%2): Weapon=%3 Armor=%4 Accessory=%5 Materia=[%6]")
            .arg(charId)
            .arg(FF7Char::defaultName(charId))
            .arg(newWeapon)
            .arg(newArmor)
            .arg(newAccessory == 255 ? "None" : QString::number(newAccessory))
            .arg(materiaLog.isEmpty() ? "none" : materiaLog.join(", ")));
    }
}

void StartingEquipmentRandomizer::randomizeCharacterEquipment(QByteArray& data, int characterId)
{
    // Now handled in randomizeStartingEquipment
    Q_UNUSED(data);
    Q_UNUSED(characterId);
}

quint16 StartingEquipmentRandomizer::getRandomWeapon(int characterId, int tier)
{
    if (tier < 0 || tier > 2) tier = 1; // Default to balanced tier
    
    if (!m_weaponPools[tier].contains(characterId) || 
        m_weaponPools[tier][characterId].isEmpty()) {
        return 1; // Default weapon
    }
    
    const QVector<quint16>& weapons = m_weaponPools[tier][characterId];
    std::uniform_int_distribution<int> dist(0, weapons.size() - 1);
    return weapons[dist(m_rng)];
}

quint16 StartingEquipmentRandomizer::getRandomArmor(int tier)
{
    if (tier < 0 || tier > 2) tier = 1;
    
    if (m_armorPools[tier].isEmpty()) {
        return 1; // Default armor
    }
    
    std::uniform_int_distribution<int> dist(0, m_armorPools[tier].size() - 1);
    return m_armorPools[tier][dist(m_rng)];
}

quint16 StartingEquipmentRandomizer::getRandomAccessory(int tier)
{
    if (tier < 0 || tier > 2) tier = 1;
    
    if (m_accessoryPools[tier].isEmpty()) {
        return 1; // Default accessory
    }
    
    std::uniform_int_distribution<int> dist(0, m_accessoryPools[tier].size() - 1);
    return m_accessoryPools[tier][dist(m_rng)];
}

void StartingEquipmentRandomizer::randomizeMateria(QByteArray& data, int characterId)
{
    const Config& config = m_parent->m_config;
    int tier = config.getStartingEquipmentTier();
    
    if (tier < 0 || tier > 2) tier = 1;
    
    if (m_materiaPools[tier].isEmpty()) {
        return; // No materia to assign
    }
    
    const int equipmentDataOffset = 0x3000;
    const int characterEntrySize = 32;
    int offset = equipmentDataOffset + (characterId * characterEntrySize) + 6; // Materia starts at offset 6
    
    // Assign 8 materia slots
    for (int i = 0; i < 8; ++i) {
        int materiaOffset = offset + (i * 2);
        
        if (materiaOffset + 2 > data.size()) {
            break;
        }
        
        // 30% chance for empty slot
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(m_rng) < 0.3) {
            data[materiaOffset] = 0xFF; // Empty slot
            data[materiaOffset + 1] = 0xFF;
        } else {
            std::uniform_int_distribution<int> materiaDist(0, m_materiaPools[tier].size() - 1);
            quint16 materiaId = m_materiaPools[tier][materiaDist(m_rng)];
            
            data[materiaOffset] = static_cast<char>(materiaId & 0xFF);
            data[materiaOffset + 1] = static_cast<char>((materiaId >> 8) & 0xFF);
        }
    }
}

void StartingEquipmentRandomizer::initializeEquipmentPools()
{
    // Weak tier (0) - Basic starting equipment
    m_weaponPools[0][Cloud] = {1, 2};        // Buster Sword, Iron Bar
    m_weaponPools[0][Barret] = {101, 102};   // Gatling Gun, Machine Gun
    m_weaponPools[0][Tifa] = {201, 202};     // Knuckles, Metal Knuckle
    
    m_armorPools[0] = {301, 302};            // Bronze Bangle, Iron Bangle
    m_accessoryPools[0] = {401, 402};        // Power Wrist, Guard Source
    m_materiaPools[0] = {501, 502};          // Fire, Ice
    
    // Balanced tier (1) - Mid-tier equipment
    m_weaponPools[1][Cloud] = {3, 4, 5};     // Mythril Saber, Hardedge, Butterfly Edge
    m_weaponPools[1][Barret] = {103, 104};   // W Machine Gun, Atomic Scissors
    m_weaponPools[1][Tifa] = {203, 204};     // Tiger Fang, Dragon Claw
    
    m_armorPools[1] = {303, 304, 305};       // Mythril Armlet, Titanium Bangle, Silver Armlet
    m_accessoryPools[1] = {403, 404, 405};   // Tatoo, Jem Ring, White Cape
    m_materiaPools[1] = {503, 504, 505};     // Lightning, Earth, Restore
    
    // Strong tier (2) - Advanced starting equipment
    m_weaponPools[2][Cloud] = {6, 7, 8};     // Enhance Sword, Organics, Crystal Sword
    m_weaponPools[2][Barret] = {105, 106};   // Heated Drill, Pile Bunker
    m_weaponPools[2][Tifa] = {205, 206};     // Master Fist, God's Hand
    
    m_armorPools[2] = {306, 307, 308};       // Gold Armlet, Diamond Bangle, Platinum Bangle
    m_accessoryPools[2] = {406, 407, 408};   // Fairy Ring, Peace Ring, Ribbon
    m_materiaPools[2] = {506, 507, 508};     // Heal, Revive, Barrier
}

bool StartingEquipmentRandomizer::replaceStartingEquipmentText()
{
    TextReplacementConfig config;
    TextReplacementConfig::ReplacementSettings settings = config.getSettings();
    
    if (!settings.enabled) {
        return true;
    }
    
    // Load KERNEL.BIN for text editing
    KernelBinParser kernelParser;
    QString kernelPath = m_parent->getFF7Path() + "/data/lang-en/kernel/kernel.bin";
    
    // Try multiple possible kernel paths
    QStringList kernelPaths = {
        m_parent->getFF7Path() + "/data/lang-en/kernel/kernel.bin",
        m_parent->getFF7Path() + "/data/kernel.bin",
        m_parent->getFF7Path() + "/kernel.bin"
    };
    
    bool kernelLoaded = false;
    for (const QString& path : kernelPaths) {
        if (QFile::exists(path)) {
            kernelPath = path;
            kernelLoaded = kernelParser.load(path);
            break;
        }
    }
    
    if (!kernelLoaded) {
        qWarning() << "Failed to load KERNEL.BIN for equipment text replacement";
        return false;
    }
    
    if (!kernelParser.loadAllItemText()) {
        qWarning() << "Failed to load item text for equipment replacement";
        return false;
    }
    
    // Replace text for randomized starting equipment
    for (int characterId = 0; characterId < 9; characterId++) {
        // Replace weapon text
        if (settings.weapons && m_randomizedWeapons.contains(characterId)) {
            quint16 weaponId = m_randomizedWeapons[characterId];
            QString newName = "Random Weapon " + QString::number(characterId + 1);
            kernelParser.replaceWeaponName(weaponId, newName);
        }
        
        // Replace armor text
        if (settings.armor && m_randomizedArmor.contains(characterId)) {
            quint16 armorId = m_randomizedArmor[characterId];
            QString newName = "Random Armor " + QString::number(characterId + 1);
            kernelParser.replaceArmorName(armorId, newName);
        }
        
        // Replace accessory text
        if (settings.accessories && m_randomizedAccessories.contains(characterId)) {
            quint16 accessoryId = m_randomizedAccessories[characterId];
            QString newName = "Random Accessory " + QString::number(characterId + 1);
            kernelParser.replaceAccessoryName(accessoryId, newName);
        }
        
        // Replace materia text
        if (settings.materia && m_randomizedMateria.contains(characterId)) {
            for (quint16 materiaId : m_randomizedMateria[characterId]) {
                QString newName = "Random Materia " + QString::number(materiaId % 10 + 1);
                kernelParser.replaceMateriaName(materiaId, newName);
            }
        }
    }
    
    // Save modified KERNEL.BIN
    QString outputPath = m_parent->getOutputPath() + "/data/lang-en/kernel/kernel2.bin";
    return kernelParser.saveAllItemText();
}

void StartingEquipmentRandomizer::replaceItemTextByCategory(quint16 itemId, const TextReplacementConfig::ReplacementSettings& settings, KernelBinParser& kernelParser)
{
    // TODO: Fix this method - temporarily disabled due to compilation errors
    Q_UNUSED(itemId)
    Q_UNUSED(settings)
    Q_UNUSED(kernelParser)
    /*
    ItemCategory category = TextEncoder::getItemCategory(itemId);
    QString prefix = getItemPrefix(category, settings);
    QString newName = generateReplacementName(itemId, category, prefix);
    
    switch (category) {
        case ItemCategory::Weapon:
            if (settings.weapons) kernelParser.replaceWeaponName(itemId, newName);
            break;
        case ItemCategory::Armor:
            if (settings.armor) kernelParser.replaceArmorName(itemId, newName);
            break;
        case ItemCategory::Accessory:
            if (settings.accessories) kernelParser.replaceAccessoryName(itemId, newName);
            break;
        case ItemCategory::Materia:
            if (settings.materia) kernelParser.replaceMateriaName(itemId, newName);
            break;
        case ItemCategory::KeyItem:
            if (settings.keyItems) kernelParser.replaceKeyItemName(itemId, newName);
            break;
        default:
            if (settings.items) kernelParser.replaceItemText(itemId, newName, category);
            break;
    }
    */
}

QString StartingEquipmentRandomizer::getItemPrefix(ItemCategory category, const TextReplacementConfig::ReplacementSettings& settings)
{
    switch (category) {
        case ItemCategory::Weapon: return settings.weaponPrefix;
        case ItemCategory::Armor: return settings.armorPrefix;
        case ItemCategory::Accessory: return settings.accessoryPrefix;
        case ItemCategory::Materia: return settings.materiaPrefix;
        case ItemCategory::KeyItem: return settings.keyItemPrefix;
        default: return settings.itemPrefix;
    }
}

QString StartingEquipmentRandomizer::generateReplacementName(quint16 itemId, ItemCategory category, const QString& prefix)
{
    // Generate descriptive names for randomized starting equipment
    switch (category) {
        case ItemCategory::Weapon:
            return prefix + " Weapon " + QString::number(itemId % 10 + 1);
        case ItemCategory::Armor:
            return prefix + " Armor " + QString::number(itemId % 10 + 1);
        case ItemCategory::Accessory:
            return prefix + " Accessory " + QString::number(itemId % 10 + 1);
        case ItemCategory::Materia:
            return prefix + " Materia " + QString::number(itemId % 10 + 1);
        case ItemCategory::KeyItem:
            return prefix + " Key Item " + QString::number(itemId % 10 + 1);
        default:
            return prefix + " Item " + QString::number(itemId % 10 + 1);
    }
}
