#include "CraterBarrierPatcher.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

namespace {

// Barrier load anchor: push_const(24) ; load_model
//   10 01  18 00     -> push_const 0x18 (north_crater_barrier = model 24)
//   00 03            -> load_model
const QByteArray kBarrierLoad = QByteArray::fromHex("100118000003");

// The 14-byte condition block sits immediately before the anchor:
//   [savemap read : 4][push_const : 4][compare : 2][goto_if_false : 4]
// We only rewrite the first 10 bytes (read + push_const + compare); the
// goto_if_false (opcode + branch target) is preserved untouched.
const int kCondBlockBack = 16; // anchor - 16 = start of savemap read
const int kCondRewrite   = 10; // bytes we overwrite

// Vanilla: if Savemap.game_progress < 1580
//   1C 01 00 00  -> push_savemap_word  0x000 (game_progress)
//   10 01 2C 06  -> push_const         1580
//   60 00        -> less_than
const QByteArray kVanilla  = QByteArray::fromHex("1C01000010012C066000");

// Modified: if Savemap[0xD27].byte == 0   (0x183 = 0xD27 - 0xBA4)
//   18 01 83 01  -> push_savemap_byte  0x183 (crater_lock)
//   10 01 00 00  -> push_const         0
//   70 00        -> equal
const QByteArray kModified = QByteArray::fromHex("18018301100100007000");

// --- Free Roam Diamond Weapon spawn (wm0.ev "Enter from field 51" handler) ---
// Unique 16-byte anchor: push_const(Highwind=3); load_model; enter_vehicle;
//                        (00 01); push_const(Diamond Weapon=10); load_model
//   10 01 03 00 | 00 03 | 0c 03 | 00 01 | 10 01 0a 00 | 00 03
const QByteArray kDiamondAnchor = QByteArray::fromHex("100103000003" "0c03" "0001" "10010a00" "0003");

// The inner condition sits 10 bytes before the anchor:
//   14 01 FC 03  -> push_savemap_bit  (vehicle_display.bit[4])
// We rewrite it to an always-false constant so goto_if_false always skips the
// Highwind load + enter_vehicle + Diamond Weapon load:
//   10 01 00 00  -> push_const        0
const int        kDiamondCondBack = 10; // anchor - 10 = start of the bit test
const QByteArray kDiamondVanilla  = QByteArray::fromHex("1401fc03"); // push bit
const QByteArray kDiamondModified = QByteArray::fromHex("10010000"); // push_const 0

// --- Free Roam crater landing (wm0.ev System fn 9 "crater_landing") ----------
//   word game_progress ; push_const 1620 ; greater_equal
//   1C 01 00 00 | 10 01 54 06 | 63 00
// At game moment 1603 this is never true, so the Highwind descent never fires.
// Lower the threshold 1620 (0x0654) -> 1580 (0x062C); 1603 >= 1580 passes.
// Length-preserving (only the constant changes); unique anchor.
const QByteArray kCraterLandVanilla  = QByteArray::fromHex("1C010000100154066300"); // gp >= 1620
const QByteArray kCraterLandModified = QByteArray::fromHex("1C01000010012C066300"); // gp >= 1580

} // namespace

CraterBarrierPatcher::CraterBarrierPatcher(const QString& ff7Path, const QString& outputPath)
    : m_ff7Path(ff7Path)
    , m_outputPath(outputPath)
{
}

quint32 CraterBarrierPatcher::readU32(const QByteArray& d, int off)
{
    if (off + 4 > d.size()) return 0;
    return  (static_cast<quint8>(d[off]))
          | (static_cast<quint8>(d[off + 1]) << 8)
          | (static_cast<quint8>(d[off + 2]) << 16)
          | (static_cast<quint8>(d[off + 3]) << 24);
}

bool CraterBarrierPatcher::findWm0(const QByteArray& lgp, int& dataStart, int& dataSize) const
{
    // LGP layout: 12-byte creator, 4-byte file count, then N x 27-byte ToC
    // entries [20 name][4 offset][1 check][2 conflict]. Each file body is
    // [20 name][4 size][data].
    if (lgp.size() < 0x10) return false;
    const quint32 numFiles = readU32(lgp, 0x0C);
    const int toc = 0x10;
    if (numFiles == 0 || numFiles > 100000) return false;
    if (toc + static_cast<int>(numFiles) * 27 > lgp.size()) return false;

    for (quint32 i = 0; i < numFiles; ++i) {
        const int entry = toc + static_cast<int>(i) * 27;
        QByteArray name = lgp.mid(entry, 20);
        int nul = name.indexOf('\0');
        if (nul >= 0) name.truncate(nul);
        if (QString::fromLatin1(name).compare(QStringLiteral("wm0.ev"), Qt::CaseInsensitive) == 0) {
            const quint32 fileOff = readU32(lgp, entry + 20);
            if (static_cast<int>(fileOff) + 24 > lgp.size()) return false;
            const quint32 size = readU32(lgp, fileOff + 20);
            dataStart = static_cast<int>(fileOff) + 24;
            dataSize  = static_cast<int>(size);
            if (dataStart + dataSize > lgp.size()) return false;
            return true;
        }
    }
    return false;
}

int CraterBarrierPatcher::patchWorldScript(QByteArray& lgp, bool& ok) const
{
    ok = false;
    int dataStart = 0, dataSize = 0;
    if (!findWm0(lgp, dataStart, dataSize)) {
        qDebug() << "CraterBarrierPatcher: wm0.ev not found in world_us.lgp";
        return 0;
    }

    const int dataEnd = dataStart + dataSize;
    int patched = 0;
    int found   = 0;

    int from = dataStart;
    while (true) {
        const int anchor = lgp.indexOf(kBarrierLoad, from);
        if (anchor < 0 || anchor >= dataEnd) break;
        from = anchor + kBarrierLoad.size();
        ++found;

        const int condStart = anchor - kCondBlockBack;
        if (condStart < dataStart) {
            qDebug() << "CraterBarrierPatcher: barrier load @0x" + QString::number(anchor, 16)
                     << "too close to wm0.ev start; skipping";
            continue;
        }

        const QByteArray cur = lgp.mid(condStart, kCondRewrite);
        if (cur == kModified) {
            qDebug() << "CraterBarrierPatcher: site @0x" + QString::number(anchor, 16)
                     << "already patched; skipping";
            continue;
        }
        if (cur != kVanilla) {
            qDebug() << "CraterBarrierPatcher: site @0x" + QString::number(anchor, 16)
                     << "has unexpected condition" << cur.toHex(' ')
                     << "- skipping (fail safe)";
            continue;
        }

        lgp.replace(condStart, kCondRewrite, kModified);
        ++patched;
        qDebug() << "CraterBarrierPatcher: patched barrier gate @0x"
                 + QString::number(anchor, 16);
    }

    if (found == 0) {
        qDebug() << "CraterBarrierPatcher: no barrier load (load_model 24) found in wm0.ev";
        return 0;
    }

    ok = true; // structure recognised; patched may be 0 if already done
    return patched;
}

int CraterBarrierPatcher::patchDiamondWeaponSpawn(QByteArray& lgp) const
{
    int dataStart = 0, dataSize = 0;
    if (!findWm0(lgp, dataStart, dataSize)) {
        qDebug() << "CraterBarrierPatcher(diamond): wm0.ev not found in world_us.lgp";
        return 0;
    }
    const int dataEnd = dataStart + dataSize;

    int patched = 0;
    int from = dataStart;
    while (true) {
        const int anchor = lgp.indexOf(kDiamondAnchor, from);
        if (anchor < 0 || anchor >= dataEnd) break;
        from = anchor + kDiamondAnchor.size();

        const int condStart = anchor - kDiamondCondBack;
        if (condStart < dataStart) {
            qDebug() << "CraterBarrierPatcher(diamond): anchor @0x" + QString::number(anchor, 16)
                     << "too close to wm0.ev start; skipping";
            continue;
        }

        const QByteArray cur = lgp.mid(condStart, kDiamondModified.size());
        if (cur == kDiamondModified) {
            qDebug() << "CraterBarrierPatcher(diamond): site @0x" + QString::number(anchor, 16)
                     << "already patched; skipping";
            continue;
        }
        if (cur != kDiamondVanilla) {
            qDebug() << "CraterBarrierPatcher(diamond): site @0x" + QString::number(anchor, 16)
                     << "has unexpected condition" << cur.toHex(' ')
                     << "- skipping (fail safe)";
            continue;
        }

        lgp.replace(condStart, kDiamondModified.size(), kDiamondModified);
        ++patched;
        qDebug() << "CraterBarrierPatcher(diamond): neutralized Diamond Weapon spawn @0x"
                 + QString::number(anchor, 16);
    }

    return patched;
}

int CraterBarrierPatcher::patchCraterLanding(QByteArray& lgp) const
{
    int dataStart = 0, dataSize = 0;
    if (!findWm0(lgp, dataStart, dataSize)) {
        qDebug() << "CraterBarrierPatcher(landing): wm0.ev not found";
        return 0;
    }
    const int dataEnd = dataStart + dataSize;

    const int at = lgp.indexOf(kCraterLandVanilla, dataStart);
    if (at < 0 || at >= dataEnd) {
        const int mod = lgp.indexOf(kCraterLandModified, dataStart);
        if (mod >= 0 && mod < dataEnd)
            qDebug() << "CraterBarrierPatcher(landing): already patched; skipping";
        else
            qDebug() << "CraterBarrierPatcher(landing): crater-landing gate (gp>=1620) not found";
        return 0;
    }
    lgp.replace(at, kCraterLandVanilla.size(), kCraterLandModified);
    qDebug() << "CraterBarrierPatcher(landing): lowered crater-landing gate 1620->1580 @0x"
             + QString::number(at, 16);
    return 1;
}

bool CraterBarrierPatcher::patch()
{
    const QString src = QDir(m_ff7Path).filePath("data/wm/world_us.lgp");
    const QString dst = QDir(m_outputPath).filePath("data/wm/world_us.lgp");

    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        qDebug() << "CraterBarrierPatcher: cannot open source world_us.lgp at" << src;
        return false;
    }
    QByteArray lgp = in.readAll();
    in.close();

    bool ok = false;
    m_sitesPatched = patchWorldScript(lgp, ok);
    if (!ok) {
        qDebug() << "CraterBarrierPatcher: world_us.lgp structure not recognised — not writing output";
        return false;
    }

    // Free Roam: also neutralize the moment-1603 Diamond Weapon / forced-Highwind
    // spawn on entry from field 51. Non-fatal if absent (logged inside).
    m_diamondSitesPatched = patchDiamondWeaponSpawn(lgp);

    // Free Roam: lower the Northern Crater landing gate (gp>=1620) so the Highwind
    // descent fires at game moment 1603. Non-fatal if absent (logged inside).
    m_craterLandingPatched = patchCraterLanding(lgp);

    // Ensure data/wm exists, then write the (possibly already-correct) LGP.
    QFileInfo fi(dst);
    QDir dir = fi.absoluteDir();
    if (!dir.exists() && !dir.mkpath(".")) {
        qDebug() << "CraterBarrierPatcher: cannot create output dir" << dir.absolutePath();
        return false;
    }

    QFile out(dst);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "CraterBarrierPatcher: cannot write" << dst;
        return false;
    }
    out.write(lgp);
    out.close();

    qDebug() << "CraterBarrierPatcher: wrote" << dst
             << "(" << m_sitesPatched << "barrier site(s),"
             << m_diamondSitesPatched << "Diamond Weapon site(s),"
             << m_craterLandingPatched << "crater-landing site(s) newly patched)";
    return true;
}
