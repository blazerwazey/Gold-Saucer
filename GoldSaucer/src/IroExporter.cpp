#include "IroExporter.h"
#include "Config.h"
#include "MakouLgpManager.h"

#include <LZS>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QDirIterator>
#include <QDateTime>
#include <QtEndian>
#include <cstring>

// ============================================================================
// .iro (IROS) archive format  — see 7th Heaven / Iros (7thWrapperLib/IrosArc.cs)
//
//   Header (16 bytes, all int32 little-endian):
//     +0  signature = 0x534F5249  ("IROS")
//     +4  version   = 0x00010002  (we write v1.2; offsets are int64 for >=0x10001)
//     +8  flags     = 0
//     +12 directoryOffset  (filled in after the file data is written)
//   File data:  every file's raw bytes, concatenated, starting at offset 16.
//   Directory (at directoryOffset):
//     int32 fileCount
//     per entry:
//       u16   entrySize   (= 20 + nameBytes; total record length incl. this field)
//       u16   nameBytes   (length of the UTF-16LE path in bytes)
//       bytes name        (UTF-16LE, BACKSLASH-separated, relative to mod root)
//       i32   flags       (0 = stored uncompressed)
//       i64   offset      (data position from start of file)
//       i32   length      (stored length; == raw length since we don't compress)
//   Write order: header -> file data -> directory; then patch directoryOffset.
//
// Mod layout (verified against a real installed 7th Heaven mod):
//   - Field script edits override one section of a field via
//       flevel.lgp/<field>.chunk.1     (.chunk.1 = section 1 = field script/dialog;
//                                        an animation mod uses .chunk.3 = models)
//     The chunk file is the RAW decompressed section data — no compression and no
//     4-byte section length prefix.
//   - World-map script:  world_us.lgp/wm0.ev   (whole, uncompressed lgp entry)
//   - Battle / kernel:    mirrored under their data-relative path (battle/, kernel/)
//   - Shop (FFNx hext):   hext/** copied verbatim
//   - mod.xml at the archive root.
// NOTE: the non-field override paths (battle/kernel) are the conventional ones but
// have not been verified against this exact 7th Heaven build — confirm on first
// import and adjust the modRel paths in stageScene/stageKernel if needed.
// ============================================================================

namespace {
constexpr quint32 IRO_SIG     = 0x534F5249u;
constexpr qint32  IRO_VERSION = 0x00010002;

// A stable mod ID so 7th Heaven treats re-generated seeds as the same mod (updates
// replace, rather than stack). Arbitrary but fixed GUID for the AP randomizer.
const char* const MOD_ID = "7A5C0DE0-FF77-4A11-9E0A-AA7C0FF7AAAA";
}

IroExporter::IroExporter(const QString& ff7Path, const QString& outputPath)
    : m_ff7Path(ff7Path)
    , m_outputPath(outputPath)
{
    m_staging = QDir(m_outputPath).filePath(".iro_build");
}

QString IroExporter::resolveOriginalFlevel() const
{
    const QStringList candidates = {
        m_ff7Path + "/ff7/workingdir/data/field/flevel.lgp",  // 2026 re-release
        m_ff7Path + "/data/field/flevel.lgp",                 // classic Steam/1998
        m_ff7Path + "/data/flevel/flevel.lgp",
    };
    for (const QString& c : candidates)
        if (QFile::exists(c))
            return c;
    return QString();
}

bool IroExporter::stageBytes(const QString& modRelPath, const QByteArray& bytes)
{
    QString dst = QDir(m_staging).filePath(modRelPath);
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile f(dst);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(bytes);
    f.close();
    return true;
}

// flevel.lgp/<field>.chunk.1 for every field the randomizer actually changed,
// found by diffing the output flevel.lgp against the original (unmodified fields
// are written back byte-identical, so a raw-entry compare is exact).
int IroExporter::stageFields(QStringList& log)
{
    const QString outFlevel = m_outputPath + "/data/field/flevel.lgp";
    if (!QFile::exists(outFlevel))
        return 0;  // field randomization was not run
    const QString origFlevel = resolveOriginalFlevel();
    if (origFlevel.isEmpty()) {
        log << "  IRO: original flevel.lgp not found — cannot diff field changes";
        return 0;
    }

    MakouLgpManager orig, out;
    if (!orig.open(origFlevel) || !out.open(outFlevel)) {
        log << "  IRO: could not open flevel.lgp for diffing";
        return 0;
    }

    int count = 0;
    for (const QString& field : out.fileList()) {
        if (field.startsWith("blackbg"))
            continue;
        QByteArray o = orig.fileData(field);
        QByteArray n = out.fileData(field);
        if (o == n)
            continue;  // unchanged field

        QByteArray dec = LZS::decompressAllWithHeader(n);
        if (dec.size() < 14)
            continue;
        // Field file header: nine u32 section offsets begin at +6.
        quint32 s0 = 0, s1 = 0;
        memcpy(&s0, dec.constData() + 6, 4);
        memcpy(&s1, dec.constData() + 10, 4);
        int start = static_cast<int>(s0) + 4;       // skip the u32 section length
        int end   = static_cast<int>(s1);           // section 2's length prefix
        if (start < 0 || start >= dec.size() || end <= start || end > dec.size())
            continue;
        QByteArray chunk = dec.mid(start, end - start);
        if (stageBytes("flevel.lgp/" + field + ".chunk.1", chunk))
            ++count;
    }
    if (count)
        log << QString("  IRO: staged %1 field script override(s) (flevel.lgp/*.chunk.1)")
                   .arg(count);
    return count;
}

// world_us.lgp/wm0.ev — the crater-barrier patch lives in wm0.ev, stored
// uncompressed in world_us.lgp (CraterBarrierPatcher edits it in place).
int IroExporter::stageWorldScript(QStringList& log)
{
    const QString outWorld = m_outputPath + "/data/wm/world_us.lgp";
    if (!QFile::exists(outWorld))
        return 0;
    MakouLgpManager lgp;
    if (!lgp.open(outWorld))
        return 0;
    QByteArray wm0 = lgp.fileData("wm0.ev");
    if (wm0.isEmpty())
        return 0;
    if (!stageBytes("world_us.lgp/wm0.ev", wm0))
        return 0;
    log << "  IRO: staged world-map script override (world_us.lgp/wm0.ev)";
    return 1;
}

// Copy a single output data file to its mod-relative override path.
int IroExporter::stageDataFile(const QString& outRelPath,
                               const QString& modRelPath, QStringList& log)
{
    const QString src = QDir(m_outputPath).filePath(outRelPath);
    if (!QFile::exists(src))
        return 0;
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly))
        return 0;
    QByteArray bytes = in.readAll();
    in.close();
    if (!stageBytes(modRelPath, bytes))
        return 0;
    log << QString("  IRO: staged %1").arg(modRelPath);
    return 1;
}

// hext/** copied verbatim (FFNx executable patches, e.g. shop randomization).
int IroExporter::stageHext(QStringList& log)
{
    const QString src = QDir(m_outputPath).filePath("hext");
    if (!QDir(src).exists())
        return 0;
    int count = 0;
    if (!copyTree(src, m_staging, "hext", count))
        return 0;
    if (count)
        log << QString("  IRO: staged %1 hext patch file(s)").arg(count);
    return count;
}

bool IroExporter::copyTree(const QString& srcDir, const QString& dstRoot,
                           const QString& modPrefix, int& count)
{
    QDir base(srcDir);
    QDirIterator it(srcDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString abs = it.next();
        QString rel = base.relativeFilePath(abs);
        QString dst = QDir(dstRoot).filePath(modPrefix + "/" + rel);
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (QFile::copy(abs, dst))
            ++count;
    }
    return true;
}

bool IroExporter::writeModXml(const Config& config, int fieldCount)
{
    const QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString name = "FF7 Archipelago Randomized";
    if (config.getSeed() != 0)
        name += QString(" (Seed %1)").arg(config.getSeed());

    QString xml;
    xml += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    xml += "<ModInfo>\n";
    xml += QString("  <ID>%1</ID>\n").arg(MOD_ID);
    xml += QString("  <Name>%1</Name>\n").arg(name);
    xml += "  <Author>Gold Saucer (Archipelago)</Author>\n";
    xml += "  <Version>1.00</Version>\n";
    xml += QString("  <ReleaseDate>%1</ReleaseDate>\n").arg(date);
    xml += "  <Category>Gameplay</Category>\n";
    xml += "  <Description>Auto-generated FF7 Archipelago randomized files "
           "(field scripts, world map, battles, kernel, shops). Built by Gold "
           "Saucer from your own game data.</Description>\n";
    xml += QString("  <ReleaseNotes>%1 field script override(s).</ReleaseNotes>\n")
               .arg(fieldCount);
    xml += "</ModInfo>\n";

    return stageBytes("mod.xml", xml.toUtf8());
}

// Walk the staging tree and write the IROS archive.
bool IroExporter::packArchive(const QString& iroPath, int& fileCount, QStringList& log)
{
    // Collect staged files (relative path with backslashes + absolute source).
    struct Entry { QString name; QString abs; qint64 offset = 0; qint64 length = 0; };
    QVector<Entry> entries;
    QDir base(m_staging);
    QDirIterator it(m_staging, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString abs = it.next();
        Entry e;
        e.abs  = abs;
        e.name = base.relativeFilePath(abs);
        e.name.replace('/', '\\');               // 7th Heaven uses backslashes
        e.length = QFileInfo(abs).size();
        entries.push_back(e);
    }
    fileCount = entries.size();
    if (entries.isEmpty()) {
        log << "  IRO: nothing to pack (no randomized output found)";
        return false;
    }

    QFile out(iroPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        log << "  IRO: could not create " + iroPath;
        return false;
    }

    auto putI32 = [&](qint32 v) {
        char b[4]; qToLittleEndian<qint32>(v, b); out.write(b, 4);
    };
    auto putU16 = [&](quint16 v) {
        char b[2]; qToLittleEndian<quint16>(v, b); out.write(b, 2);
    };
    auto putI64 = [&](qint64 v) {
        char b[8]; qToLittleEndian<qint64>(v, b); out.write(b, 8);
    };

    // Header (directoryOffset patched later).
    putI32(static_cast<qint32>(IRO_SIG));
    putI32(IRO_VERSION);
    putI32(0);            // flags
    putI32(0);            // directoryOffset placeholder

    // File data.
    for (Entry& e : entries) {
        e.offset = out.pos();
        QFile in(e.abs);
        if (!in.open(QIODevice::ReadOnly)) {
            log << "  IRO: failed to read staged file " + e.abs;
            out.close();
            return false;
        }
        out.write(in.readAll());
        in.close();
    }

    // Directory.
    qint64 dirOffset = out.pos();
    putI32(static_cast<qint32>(entries.size()));
    for (const Entry& e : entries) {
        QByteArray nameBytes(reinterpret_cast<const char*>(e.name.utf16()),
                             e.name.size() * 2);   // UTF-16LE
        quint16 entrySize = static_cast<quint16>(2 + 2 + nameBytes.size() + 4 + 8 + 4);
        putU16(entrySize);
        putU16(static_cast<quint16>(nameBytes.size()));
        out.write(nameBytes);
        putI32(0);                                 // flags: stored uncompressed
        putI64(e.offset);
        putI32(static_cast<qint32>(e.length));
    }

    // Patch directoryOffset.
    out.seek(12);
    putI32(static_cast<qint32>(dirOffset));
    out.close();
    return true;
}

bool IroExporter::exportIro(const QString& iroPath, const Config& config, QStringList& log)
{
    // Fresh staging tree.
    QDir(m_staging).removeRecursively();
    QDir().mkpath(m_staging);

    int fields = stageFields(log);
    stageWorldScript(log);
    // Battle + kernel: 2026 re-release nests these under data/lang-en; mirror them
    // to the conventional FFNx mod-relative paths.
    stageDataFile("data/lang-en/battle/scene.bin", "battle/scene.bin", log);
    stageDataFile("data/battle/scene.bin",         "battle/scene.bin", log);
    stageDataFile("data/lang-en/kernel/kernel.bin", "kernel/kernel.bin", log);
    stageDataFile("data/lang-en/kernel/KERNEL.BIN", "kernel/kernel.bin", log);
    stageDataFile("data/kernel/kernel.bin",         "kernel/kernel.bin", log);
    stageDataFile("data/lang-en/kernel/kernel2.bin", "kernel/kernel2.bin", log);
    stageHext(log);

    writeModXml(config, fields);

    int fileCount = 0;
    if (!packArchive(iroPath, fileCount, log)) {
        QDir(m_staging).removeRecursively();
        return false;
    }

    QDir(m_staging).removeRecursively();
    log << QString("  IRO: wrote %1 (%2 file(s))")
               .arg(QFileInfo(iroPath).fileName()).arg(fileCount);
    return true;
}
