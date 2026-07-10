#include "CraterBarrierPatcher.h"
#include "ApSeedFile.h"
#include "WorldScriptEditor.h"
#include <QtGlobal>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>

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

// --- Free Roam ambient Diamond Weapon spawns (wm0.ev overworld model loader) --
// Diamond Weapon (model 10) is loaded in two overworld progress blocks, each
// gated on the disc-2 story flag Savemap[0xEF6].bit[3] (the "Diamond marches on
// Midgar" flag set on leaving the Forgotten Capital):
//   14 01 93 1a  -> push_savemap_bit 6803 (0xEF6.3)
//   01 02 ?? ??  -> goto_if_false <skip>
//   00 01        -> reset
//   10 01 0a 00  -> push_const 10 (Diamond Weapon)
//   00 03        -> load_model
// We anchor on the model-10 load and back-walk this exact shape, then rewrite
// the bit test to push_const 0 so the goto_if_false always skips the load —
// Diamond never rises from the ocean (his world-map model does not render in
// Free Roam even at world_progress 4, so he is fully hidden rather than spawned).
// (0xEF6.3 is read elsewhere for non-spawn logic, so we must NOT blind-replace
// the bit op — only the back-walked spawn shape qualifies.)
const QByteArray kDiamondModelLoad   = QByteArray::fromHex("10010a000003"); // push_const 10; load_model
const QByteArray kAmbientBitVanilla  = QByteArray::fromHex("1401931a");     // push bit 6803 (0xEF6.3)
const QByteArray kAmbientBitModified = QByteArray::fromHex("10010000");     // push_const 0 (always-false)
// Shape between the bit test and the load: [bit:4][goto_if_false:2+2][reset:2] = 10 bytes.
const int        kAmbientGateBack    = 10;

// --- Free Roam crater landing (wm0.ev System fn 9 "crater_landing") ----------
//   word game_progress ; push_const 1620 ; greater_equal
//   1C 01 00 00 | 10 01 54 06 | 63 00
// Vanilla gates the Highwind descent on game_progress >= 1620. In Free Roam (game
// moment 1997) that is ALWAYS true, so the player could fly to the crater and land
// even while the barrier is up. Re-gate the descent on crater_lock instead — the
// same flag the barrier model is gated on — so the descent only fires once the
// goal items are in (crater_lock=1). Length-preserving (10 bytes -> 10 bytes):
//   push_savemap_byte 0x183 (crater_lock = 0xD27-0xBA4) ; push_const 1 ; greater_equal
//   18 01 83 01 | 10 01 01 00 | 63 00   ==>  if crater_lock >= 1 then descend
const QByteArray kCraterLandVanilla  = QByteArray::fromHex("1C010000100154066300"); // gp >= 1620
const QByteArray kCraterLandModified = QByteArray::fromHex("18018301100101006300"); // crater_lock >= 1

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

    // WorldScriptEditor round-trip self-test: parse + re-emit wm0.ev with NO edits and
    // assert byte-identical, validating the re-offsetter's EV/GOTO model on the player's
    // real world script. Result written to <output>/worldscript_selftest.txt so it is
    // visible (qDebug is invisible in a GUI run).
    {
        QByteArray ev = lgp.mid(dataStart, dataSize);
        QString e;
        bool pass = WorldScriptEditor::selfTestRoundTrip(ev, e);
        QFile f(QDir(m_outputPath).filePath("worldscript_selftest.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream(&f) << "WorldScriptEditor round-trip self-test: "
                            << (pass ? "PASS" : QStringLiteral("FAIL — %1").arg(e))
                            << "  (wm0.ev " << dataSize << " bytes)\n";
        }
        qDebug() << "WorldScriptEditor self-test:" << (pass ? "PASS" : ("FAIL " + e));
    }

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

int CraterBarrierPatcher::patchDiamondAmbientSpawn(QByteArray& lgp) const
{
    int dataStart = 0, dataSize = 0;
    if (!findWm0(lgp, dataStart, dataSize)) {
        qDebug() << "CraterBarrierPatcher(diamond-ambient): wm0.ev not found in world_us.lgp";
        return 0;
    }
    const int dataEnd = dataStart + dataSize;

    int patched = 0;
    int from = dataStart;
    while (true) {
        const int load = lgp.indexOf(kDiamondModelLoad, from);
        if (load < 0 || load >= dataEnd) break;
        from = load + kDiamondModelLoad.size();

        const int bitStart = load - kAmbientGateBack;
        if (bitStart < dataStart) continue;

        // Back-walk the exact gate shape: bit 6803 ; goto_if_false ?? ; reset.
        // The goto target (2 bytes) varies per block, so check around it.
        const QByteArray bitOp = lgp.mid(bitStart, 4);          // 14 01 93 1a  /  10 01 00 00
        const bool isGotoIf    = lgp.mid(bitStart + 4, 2) == QByteArray::fromHex("0102");
        const bool isReset     = lgp.mid(bitStart + 8, 2) == QByteArray::fromHex("0001");

        if (bitOp == kAmbientBitModified && isGotoIf && isReset) {
            qDebug() << "CraterBarrierPatcher(diamond-ambient): site @0x"
                     + QString::number(load, 16) << "already patched; skipping";
            continue;
        }
        if (bitOp != kAmbientBitVanilla || !isGotoIf || !isReset) {
            // Not an ambient-spawn gate (e.g. the field-51/Highwind load, which
            // is handled by patchDiamondWeaponSpawn). Leave it alone.
            continue;
        }

        lgp.replace(bitStart, 4, kAmbientBitModified);
        ++patched;
        qDebug() << "CraterBarrierPatcher(diamond-ambient): neutralized ambient Diamond Weapon spawn @0x"
                 + QString::number(load, 16);
    }

    return patched;
}

int CraterBarrierPatcher::patchDiamondBoardingScene(QByteArray& lgp) const
{
    int dataStart = 0, dataSize = 0;
    if (!findWm0(lgp, dataStart, dataSize)) {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): wm0.ev not found";
        return 0;
    }
    const int dataEnd = dataStart + dataSize;

    // The Diamond Weapon's world-map proximity handler (world-script fn 92) invokes
    // diamond_weapon fn 28 — the rise cinematic + enter_field(highwind_bridge_4) map
    // jump — via `PUSH 29 ; CALL_FN_28` (10 01 1d 00 20 02) when the player comes
    // within 130 units of the (hidden) Diamond entity. In Free Roam this fires when
    // you board the Highwind near the Forgotten City. (The other caller, model 10 in
    // the field-51 gate, is removed by patchHighwindDiamondScene; this is the second,
    // proximity-driven one.) Neuter the call: CALL_FN_28 (20 02) -> RESET (00 01), so
    // the branch clears the stack and returns — no cutscene, no map jump. Length-
    // preserving; the enclosing weapon-battle branches are untouched.
    int patched = 0;

    // (a) Neuter the model-29 proximity caller: PUSH 29 ; CALL_FN_28 -> PUSH 29 ; RESET.
    const QByteArray callVanilla = QByteArray::fromHex("10011d002002");   // PUSH 29 ; CALL_FN_28
    const QByteArray callPatched = QByteArray::fromHex("10011d000001");   // PUSH 29 ; RESET
    const int at = lgp.indexOf(callVanilla, dataStart);
    if (at >= 0 && at < dataEnd) {
        lgp.replace(at, callVanilla.size(), callPatched);
        ++patched;
        qDebug() << "CraterBarrierPatcher(diamond-boarding): neutered Diamond proximity call @0x"
                 + QString::number(at, 16);
    } else if (lgp.indexOf(callPatched, dataStart) < 0) {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): PUSH 29 ; CALL_FN_28 not found";
    }

    // (b) DEFINITIVE: neuter diamond_weapon fn 28 (Landscaper "System 28") ITSELF — the
    // rise-from-ocean cinematic + enter_field(highwind_bridge_4) map jump that fires on
    // touching the Highwind. It's reached from multiple entry-table slots (29 & 68 both
    // point at its 0x3aa8 body), so neutering callers alone was insufficient; instead
    // write RETURN at its entry. Unique anchor = its first instrs: RESET ; set
    // Savemap[0xF28].bit[5] (PUSH_BIT 7205 ; PUSH 1 ; assign 0xe0). The preceding fn
    // already RETURNs, so the leading RESET (00 01) is safe to overwrite with
    // RETURN (03 02). Length-preserving.
    const QByteArray fn28Vanilla = QByteArray::fromHex("00011401251c10010100e000");
    const int s = lgp.indexOf(fn28Vanilla, dataStart);
    if (s >= 0 && s < dataEnd) {
        lgp[s] = char(0x03); lgp[s + 1] = char(0x02);   // RESET -> RETURN
        ++patched;
        qDebug() << "CraterBarrierPatcher(diamond-boarding): RETURN'd diamond fn 28 (System 28) @0x"
                 + QString::number(s, 16);
    } else if (lgp.indexOf(QByteArray::fromHex("03021401251c10010100e000"), dataStart) >= 0) {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): diamond fn 28 already neutered";
    } else {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): diamond fn 28 entry anchor not found";
    }

    // (c) Neuter the SEPARATE camera-follow rise cutscene (world-script fn at entry
    // slots 31/50) — this is the one the player actually sees: it runs
    // set_vertical_speed_with_follow(-30) (the Diamond "rises from the ocean" with the
    // camera following) then enter_field(59). System 28 (b) was a different sequence.
    // Write RETURN at its entry. Unique anchor = its first instrs: RESET ; PUSH 0 ;
    // op307 ; RESET ; PUSH 30 ; negate ; set_vertical_speed_with_follow (3a 03).
    const QByteArray riseVanilla = QByteArray::fromHex("0001100100000703000110011e0015003a03");
    const int r = lgp.indexOf(riseVanilla, dataStart);
    if (r >= 0 && r < dataEnd) {
        lgp[r] = char(0x03); lgp[r + 1] = char(0x02);   // RESET -> RETURN
        ++patched;
        qDebug() << "CraterBarrierPatcher(diamond-boarding): RETURN'd Diamond camera-follow rise fn @0x"
                 + QString::number(r, 16);
    } else if (lgp.indexOf(QByteArray::fromHex("0302100100000703000110011e0015003a03"), dataStart) >= 0) {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): camera-follow rise already neutered";
    } else {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): camera-follow rise anchor not found";
    }

    // (d) THE DECISIVE ONE (found by live bisect, 2026-07-07): the map jump the player
    // actually gets is the ENTER_FIELD(59) inside System fn 30's BODY. The ENGINE (its
    // C-side Diamond state machine, armed by savemap 0xEF6.3 "Diamond marching") resumes
    // execution INSIDE that body — past the RETURN written at the fn head by (c) — so no
    // head patch can stop it. Kill the jump opcode itself: RESET; PUSH 59; PUSH 0;
    // ENTER_FIELD -> replace ENTER_FIELD (18 03) with RESET (00 01). Length-preserving,
    // proven in-game. (FF7Client also keeps 0xEF6.3 clear — this is the backstop for the
    // race window before the client's next poll.)
    const QByteArray jumpVanilla = QByteArray::fromHex("000110013b00100100001803");
    const int j = lgp.indexOf(jumpVanilla, dataStart);
    if (j >= 0 && j < dataEnd) {
        lgp[j + 10] = char(0x00); lgp[j + 11] = char(0x01);   // ENTER_FIELD -> RESET
        ++patched;
        qDebug() << "CraterBarrierPatcher(diamond-boarding): killed System-30 body ENTER_FIELD(59) @0x"
                 + QString::number(j + 10, 16);
    } else if (lgp.indexOf(QByteArray::fromHex("000110013b00100100000001"), dataStart) >= 0) {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): System-30 ENTER_FIELD already killed";
    } else {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): System-30 ENTER_FIELD(59) anchor not found";
    }

    // (e) Neuter the Diamond emerge cinematic fn (entry slots 28/73): play_sfx 266,
    // set_vertical_speed(+10) emerge, sets Savemap bit 7220 "Diamond emerged". Also
    // engine-reachable; RETURN its head. Anchor extends through the PUSH 266 ; PLAY_SFX
    // to be unique (the bare head shape appears 6x in wm0.ev).
    const QByteArray emergeVanilla = QByteArray::fromHex("000110010400100100000e03000110010a011d03");
    const int e = lgp.indexOf(emergeVanilla, dataStart);
    if (e >= 0 && e < dataEnd) {
        lgp[e] = char(0x03); lgp[e + 1] = char(0x02);   // RESET -> RETURN
        ++patched;
        qDebug() << "CraterBarrierPatcher(diamond-boarding): RETURN'd Diamond emerge fn @0x"
                 + QString::number(e, 16);
    } else if (lgp.indexOf(QByteArray::fromHex("030210010400100100000e03000110010a011d03"), dataStart) >= 0) {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): Diamond emerge fn already neutered";
    } else {
        qDebug() << "CraterBarrierPatcher(diamond-boarding): Diamond emerge anchor not found";
    }

    return patched;
}

int CraterBarrierPatcher::patchHighwindDiamondScene(QByteArray& lgp) const
{
    int dataStart = 0, dataSize = 0;
    if (!findWm0(lgp, dataStart, dataSize)) {
        qDebug() << "CraterBarrierPatcher(highwind-diamond): wm0.ev not found";
        return 0;
    }
    const int dataEnd = dataStart + dataSize;

    // The Highwind model's init runs, inside "if last_field_id == 0" (Special[5]):
    //   if last_field_id == 51 then <reposition + rise cinematic + diamond_weapon fn 28> end
    // which in Free Roam fires on entry-from-field-51 and forces the Diamond scene.
    // Rather than the old "compare 51 -> 0xFFFF" no-op, cleanly DELETE the whole
    // inner-if via the re-offsetting WorldScriptEditor (RESET; PUSH last_field_id;
    // PUSH 51; EQ; GOTO_IF_FALSE; <body>), leaving the outer body as just
    // "goto label_1". Disambiguated from the other last_field_id==51 gate (the small
    // enter-vehicle path) by the diamond_weapon call (CALL_FN_28 = 0x220) in its body.
    (void)dataEnd;
    QByteArray ev = lgp.mid(dataStart, dataSize);
    WorldScriptEditor w;
    QString err;
    if (!w.parse(ev, err)) {
        qDebug() << "CraterBarrierPatcher(highwind-diamond): wm0 parse failed —" << err;
        return 0;
    }
    int startIdx = -1, keepIdx = -1;
    for (int i = w.findOpcode(0x201); i >= 0; i = w.findOpcode(0x201, i + 1)) {   // GOTO_IF_FALSE
        if (i < 4) continue;
        if (!(w.opAt(i - 1) == 0x070                                   // EQ
              && w.opAt(i - 2) == 0x110 && w.paramAt(i - 2, 0) == 51   // PUSH_CONSTANT 51
              && w.opAt(i - 3) == 0x11b && w.paramAt(i - 3, 0) == 6    // PUSH_SPECIAL_BYTE last_field_id
              && w.opAt(i - 4) == 0x100))                              // RESET
            continue;
        const int tgt = w.gotoTarget(i);
        if (tgt <= i) continue;
        bool hasDiamond = false;
        for (int b = i + 1; b < tgt; ++b) if (w.opAt(b) == 0x220) { hasDiamond = true; break; } // CALL_FN_28
        if (!hasDiamond) continue;
        startIdx = i - 4; keepIdx = tgt; break;
    }
    if (startIdx < 0) {
        qDebug() << "CraterBarrierPatcher(highwind-diamond): Highwind Diamond gate not found (already removed?)";
        return 0;
    }
    // Delete [startIdx, keepIdx): removeAt(startIdx) shifts the next instr down to
    // startIdx, so N calls remove N consecutive instructions. Abort (leave wm0
    // untouched) if any refuses — we never write a half-removed block.
    const int count = keepIdx - startIdx;
    for (int k = 0; k < count; ++k) {
        if (!w.removeAt(startIdx, err)) {
            qDebug() << "CraterBarrierPatcher(highwind-diamond): removeAt failed at" << startIdx << "—" << err;
            return 0;
        }
    }
    const QByteArray out = w.assemble(err);
    if (out.isEmpty()) {
        qDebug() << "CraterBarrierPatcher(highwind-diamond): assemble failed —" << err;
        return 0;
    }
    lgp.replace(dataStart, dataSize, out);
    qDebug() << "CraterBarrierPatcher(highwind-diamond): removed Highwind Diamond inner-if ("
             << count << "instrs)";
    return 1;
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
    qDebug() << "CraterBarrierPatcher(landing): re-gated crater descent on crater_lock @0x"
             + QString::number(at, 16);
    return 1;
}

// ----------------------------------------------------------------------------
// patchTownGates — insert an AP-key check into each gated town's world-map entry.
// PUSH_SAVEMAP_BIT bit index = relByte*8 + bit (rel to savemap bank 1 / 0xBA4),
// matching the client KEY_ITEM_FLAGS (offset,bit) it sets on key receipt.
// ----------------------------------------------------------------------------
QByteArray CraterBarrierPatcher::encodeWorldText(const QString& s)
{
    QByteArray b;
    for (QChar c : s) {
        int v = c.unicode();
        b.append(char((v >= 0x20 && v <= 0x7E) ? (v - 0x20) : 0));
    }
    return b;
}

bool CraterBarrierPatcher::overwriteWorldMessages(QByteArray& lgp, const QMap<int, QByteArray>& edits) const
{
    // locate "mes" in the lgp TOC (same layout as findWm0)
    int mesStart = 0, mesSize = 0;
    if (lgp.size() < 0x10) return false;
    const quint32 numFiles = readU32(lgp, 0x0C);
    for (quint32 i = 0; i < numFiles; ++i) {
        const int e = 0x10 + int(i) * 27;
        if (e + 24 > lgp.size()) break;
        QByteArray nm = lgp.mid(e, 20); nm = nm.left(nm.indexOf('\0') < 0 ? 20 : nm.indexOf('\0'));
        if (QString::fromLatin1(nm).compare(QStringLiteral("mes"), Qt::CaseInsensitive) == 0) {
            const int fileOff = int(readU32(lgp, e + 20));
            mesStart = fileOff + 24; mesSize = int(readU32(lgp, fileOff + 20)); break;
        }
    }
    if (mesStart == 0) return false;

    // MES: u16 numMessages, then numMessages u16 offsets -> FF7-text blobs (0xFF-term).
    // Keep the count; swap the requested blobs and re-lay the table (length-preserving).
    QByteArray mes = lgp.mid(mesStart, mesSize);
    const int mnum = quint8(mes.at(0)) | (quint8(mes.at(1)) << 8);
    QVector<QByteArray> blobs;
    for (int i = 0; i < mnum; ++i) {
        int o = quint8(mes.at(2 + 2 * i)) | (quint8(mes.at(3 + 2 * i)) << 8);
        int p = o; while (p < mes.size() && quint8(mes.at(p)) != 0xFF) ++p;
        blobs.append(mes.mid(o, p - o + 1));            // includes the 0xFF
    }
    for (auto it = edits.constBegin(); it != edits.constEnd(); ++it)
        if (it.key() >= 0 && it.key() < mnum) blobs[it.key()] = it.value();

    const int tableSize = 2 + mnum * 2;
    QByteArray nm; nm.append(char(mnum & 0xFF)); nm.append(char((mnum >> 8) & 0xFF));
    int pos = tableSize; QVector<int> offs;
    for (const QByteArray& bb : blobs) { offs.append(pos); pos += bb.size(); }
    for (int o : offs) { nm.append(char(o & 0xFF)); nm.append(char((o >> 8) & 0xFF)); }
    for (const QByteArray& bb : blobs) nm.append(bb);
    if (nm.size() > mesSize) return false;
    nm.append(QByteArray(mesSize - nm.size(), 0));
    lgp.replace(mesStart, mesSize, nm);
    return true;
}

int CraterBarrierPatcher::patchWelcomeMessage(QByteArray& lgp) const
{
    // Overwrite msg 53 (the save tutorial, shown on first world-map entry in Free
    // Roam) with the AP welcome banner, across two pages:
    //   page 1: "Welcome to FF7 {RAINBOW}Archipelago v0.0.5"
    //   page 2: "{WHITE}Please report all bugs on Github / or in the AP Discord!"
    // FF7 control codes: 0xFE 0xDB = {RAINBOW}, 0xFE 0xD9 = {WHITE}, 0xE7 = newline,
    // 0xE8 = {NEWPAGE}. Rainbow/flash can't be cancelled by a color code within a
    // page, so the page break resets it (no vanilla world message pages — if the
    // world renderer ignores 0xE8 the two pages just merge; rainbow then bleeds).
    // Lines kept <=33 visible chars to fit msg 53's window (x40 w240 h73).
    QByteArray w;
    w += encodeWorldText("Welcome to FF7 ");
    w.append(char(0xFE)); w.append(char(0xDB));                 // {RAINBOW}
    w += encodeWorldText("Archipelago v0.0.5");
    w.append(char(0xE8));                                       // {NEWPAGE} -> resets rainbow
    w.append(char(0xFE)); w.append(char(0xD9));                 // {WHITE}
    w += encodeWorldText("Please report all bugs on Github");
    w.append(char(0xE7));                                       // newline
    w += encodeWorldText("or in the AP Discord!");
    w.append(char(0xFF));                                       // terminator

    if (!overwriteWorldMessages(lgp, {{53, w}})) {
        qDebug() << "CraterBarrierPatcher: welcome banner — 'mes' not found / overflow";
        return 0;
    }
    qDebug() << "CraterBarrierPatcher: overwrote MES message id 53 (welcome banner)";
    return 1;
}

int CraterBarrierPatcher::patchTownGates(QByteArray& lgp) const
{
    int dataStart = 0, dataSize = 0;
    if (!findWm0(lgp, dataStart, dataSize)) return 0;

    QFile logf(QDir(m_outputPath).filePath("towngate.txt"));
    QTextStream log;
    if (logf.open(QIODevice::WriteOnly | QIODevice::Truncate)) log.setDevice(&logf);
    auto LOG = [&](const QString& s){ qDebug().noquote() << s; if (logf.isOpen()) log << s << "\n"; };

    // Only gate towns when the seed asked for it: read free_roam + rules.town_gating
    // straight from the .apff7 (GS's own free_roam flag is a GUI/config setting, not
    // the seed's). Absent/false -> leave the world map untouched.
    {
        const QByteArray seedJson = ApSeedFile::readJson(m_apJsonPath);
        if (seedJson.isEmpty()) {
            LOG("towngate: no .apff7 path — skipping"); return 0;
        }
        QJsonObject root = QJsonDocument::fromJson(seedJson).object();
        bool freeRoam = root.value("free_roam").toBool(false);
        bool townGating = root.value("rules").toObject().value("town_gating").toBool(false);
        if (!(freeRoam && townGating)) {
            LOG(QStringLiteral("towngate: disabled (free_roam=%1 town_gating=%2) — skipping")
                .arg(freeRoam).arg(townGating));
            return 0;
        }
    }

    struct Town { int tblIdx; int relByte; int bit; const char* name; };
    // tblIdx = the 1-based world field.tbl index the mesh ENTER_FIELD pushes
    // (verified against the shipped field.tbl + flevel maplist, 2026-07-09).
    static const Town towns[] = {
        {  6, 0x184, 0, "Fort Condor"  },  // condor1
        {  7, 0x184, 1, "Junon"        },  // ujunon1
        { 15, 0x184, 2, "North Corel"  },  // ncorel
        { 14, 0x184, 2, "Mt. Corel"    },  // mtcrl_0 (mountain path = Corel back door, same key)
        { 18, 0x184, 3, "Cosmo Canyon" },  // cos_btm
        { 19, 0x184, 4, "Nibelheim"    },  // nivl_3 (entrance 1)
        { 43, 0x184, 4, "Nibelheim"    },  // nivl_3 (entrance 2, same key)
        { 44, 0x184, 4, "Mt. Nibel"    },  // mtnvl2 (behind Nibelheim, same key)
        { 46, 0x184, 4, "Mt. Nibel"    },  // mtnvl4 (behind Nibelheim, same key)
        { 20, 0x184, 5, "Rocket Town"  },  // rckt
        { 23, 0x184, 6, "Wutai"        },  // uutai1
        // tbl#27/#47 are the Great Glacier 'snow' entries, NOT Icicle: they were
        // wrongly gated as "Icicle Inn" (logic gates the Glacier on Snowboard +
        // Glacier Map, not a town key) -> ungated. Icicle town is tbl#11.
        { 11, 0x184, 7, "Icicle Inn"   },  // itown1a
        { 13, 0x185, 0, "Mideel"       },  // del2 (was mispointed at tbl#11 = Icicle!)
        { 17, 0x185, 1, "Gongaga"      },  // gonjun2
        { 25, 0x185, 2, "Bone Village" },  // bonevil
        // The Corral Valley strip is the back door into the Sleeping Forest /
        // Bone Village / Forgotten Capital chain — seal all three world entries
        // on the Bone Village key so the area only opens through Bone Village.
        { 26, 0x185, 2, "Corral Valley Cave" },  // sandun_2 (same key)
        { 57, 0x185, 2, "Corral Valley"      },  // sango2   (same key)
        { 58, 0x185, 2, "Corral Valley"      },  // lost1    (same key)
    };
    const int nTowns = int(sizeof(towns) / sizeof(towns[0]));

    // One shared "sealed" notice, shown inline from each town's Mesh entry (the
    // overworld renders messages from mesh context — e.g. the Diamond Weapon
    // scene). We OVERWRITE an existing, already-loaded message id rather than
    // appending: the world module only loads the original message count, so an
    // appended id renders blank. id 57 is the buggy how-to tutorial — never shown
    // in Free Roam (the buggy/Tiny Bronco are never boarded; you have the Highwind).
    static const char* const kLockedMsg = "This area is sealed.";
    const int kMsgId = 57;

    // --- overwrite the chosen message id in the world MES (length-preserving) ---
    {
        QByteArray locked = encodeWorldText(QString::fromLatin1(kLockedMsg));
        locked.append(char(0xFF));
        if (!overwriteWorldMessages(lgp, {{kMsgId, locked}})) {
            LOG("towngate: world 'mes' overwrite failed (not found / overflow)"); return 0;
        }
        LOG(QStringLiteral("towngate: overwrote MES message id %1 (\"%2\")").arg(kMsgId).arg(kLockedMsg));
    }

    // --- world script edits. Replacing the MES (same size) does not shift wm0.ev. ---
    QByteArray ev = lgp.mid(dataStart, dataSize);
    WorldScriptEditor w;
    QString err;
    if (!w.parse(ev, err)) { LOG("towngate: wm0 parse failed — " + err); return 0; }

    auto W = [](QByteArray& b, int op){ b.append(char(op & 0xFF)); b.append(char((op >> 8) & 0xFF)); };
    auto PUSH = [&](QByteArray& b, int v){ W(b, 0x110); W(b, v); };

    // The inline "show the sealed message" run, mirroring the vanilla overworld
    // message shape (SET_CONTROLS to freeze the player so the window takes input;
    // two WAIT_WINDOWs so the box is ready before SET_MESSAGE / WAIT_DISMISS).
    auto msgRun = [&](int msgId){
        QByteArray b;
        W(b, 0x100); PUSH(b, 0); W(b, 0x307);                                   // SET_CONTROLS 0
        W(b, 0x100); PUSH(b, 0); PUSH(b, 0); W(b, 0x32C);                        // SET_WINDOW_STYLE 0,0
        W(b, 0x32D);                                                            // WAIT_WINDOW
        W(b, 0x100); PUSH(b, 0x23); PUSH(b, 0xA0); PUSH(b, 0xFA); PUSH(b, 0x29); W(b, 0x324); // SET_WINDOW_SIZE
        W(b, 0x32D);                                                            // WAIT_WINDOW
        W(b, 0x100); PUSH(b, msgId); W(b, 0x325);                               // SET_MESSAGE
        W(b, 0x32E);                                                            // WAIT_DISMISS
        W(b, 0x100); PUSH(b, 1); W(b, 0x307);                                   // SET_CONTROLS 1
        return b;
    };

    // Gate each town by wrapping EVERY ENTER_FIELD that loads its field. A town's
    // mesh handler can enter its field from more than one code path (e.g. North
    // Corel and Rocket Town each have two scenario ENTER_FIELDs, guarded by a
    // GOTO_IF_FALSE that targets a RESET, not a RETURN), so gating a single site or
    // relying on the existing guard's target is fragile. Instead, right before each
    // ENTER_FIELD's tblIdx PUSH we splice a self-contained gate:
    //     PUSH key
    //     GOTO_IF_FALSE -> [message run + RETURN]   ; key missing -> sealed notice
    //     GOTO -> [original PUSH tbl; PUSH scenario; ENTER_FIELD]   ; key held -> enter
    //     <message run + RETURN>
    //     <original body>
    // The message block sits inline (inside the function's parsed extent, before the
    // ENTER_FIELD), so every GOTO target stays a valid instruction boundary on a
    // re-parse. Each site is preceded by a RESET, so the stack is clean at the splice
    // point. Sites are gated high-index-first so the lower sites' indices stay valid.
    int gated = 0;
    for (int ti = 0; ti < nTowns; ++ti) {
        const Town& t = towns[ti];
        QVector<int> sites;
        for (int i = w.findOpcode(0x318); i >= 0; i = w.findOpcode(0x318, i + 1))
            if (i >= 2 && w.opAt(i - 2) == 0x110 && int(w.paramAt(i - 2, 0)) == t.tblIdx)
                sites.append(i);
        if (sites.isEmpty()) { LOG(QStringLiteral("towngate: %1 ENTER_FIELD (tbl#%2) not found").arg(t.name).arg(t.tblIdx)); continue; }

        const int keyBit = t.relByte * 8 + t.bit;
        QByteArray pushKey; W(pushKey, 0x114); W(pushKey, keyBit);       // PUSH_SAVEMAP_BIT <key>

        int done = 0;
        for (int si = sites.size() - 1; si >= 0; --si) {
            const int P = sites[si] - 2;                                 // the tblIdx PUSH_CONST
            QByteArray blk = msgRun(kMsgId); W(blk, 0x203);              // sealed message + RETURN
            if (!w.insertBefore(P, pushKey, err)) { LOG("towngate: " + QString(t.name) + " key push — " + err); break; }
            const int c1 = w.instrCount();
            if (!w.insertBefore(P + 1, blk, err)) { LOG("towngate: " + QString(t.name) + " msg insert — " + err); break; }
            const int L = w.instrCount() - c1;                           // message-block length
            if (!w.insertGoto(P + 1, /*ifFalse*/false, P + 1 + L, err)) { LOG("towngate: " + QString(t.name) + " skip-goto — " + err); break; }
            if (!w.insertGoto(P + 1, /*ifFalse*/true,  P + 2, err))      { LOG("towngate: " + QString(t.name) + " key-goto — " + err); break; }
            ++done;
        }
        if (done == 0) { LOG("towngate: " + QString(t.name) + " no sites gated"); continue; }

        LOG(QStringLiteral("towngate: gated %1 (tbl#%2, key bit 0x%3, %4 site(s), msg id %5)")
            .arg(t.name).arg(t.tblIdx).arg(keyBit,0,16).arg(done).arg(kMsgId));
        ++gated;
    }

    if (gated > 0) {
        QByteArray out = w.assemble(err);
        if (out.isEmpty()) { LOG("towngate: assemble failed — " + err); return 0; }
        lgp.replace(dataStart, dataSize, out);
    }
    LOG(QStringLiteral("towngate: %1 town(s) gated").arg(gated));
    return gated;
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

    // Diamond Weapon HIDDEN again (2026-06-20): unlike Ruby, his world-map model
    // does not render in Free Roam even at world_progress 4, so rather than spawn a
    // collidable-but-invisible boss we neutralize his ambient (0xEF6.3) spawn —
    // he never rises from the ocean. (field-51 forced-Highwind spawn stays patched
    // by patchDiamondWeaponSpawn above.)
    m_diamondAmbientPatched = patchDiamondAmbientSpawn(lgp);

    // Free Roam: also kill the Highwind-init Diamond Weapon scene (the "board the
    // Highwind after the Forgotten City" cinematic that repositions the Highwind and
    // calls diamond_weapon fn 28). Gated on last_field_id==51; we make it impossible.
    // Free Roam: re-gate the Northern Crater landing/descent on crater_lock (was
    // game_progress, always-true in Free Roam) so the Highwind can only descend once
    // the goal items are in and the barrier is down. Non-fatal if absent (logged).
    m_craterLandingPatched = patchCraterLanding(lgp);

    // Free Roam: delete the Highwind-init Diamond Weapon cinematic. This RE-OFFSETS
    // wm0.ev, so it must run AFTER all the byte-anchor patches above (barrier,
    // Diamond spawns, crater landing) — their anchors would otherwise shift.
    m_highwindScenePatched = patchHighwindDiamondScene(lgp);

    // Free Roam welcome banner: overwrite the world save-tutorial message (id 53,
    // shown on first world-map entry) with the FF7 Archipelago welcome text.
    patchWelcomeMessage(lgp);

    // Free Roam town gating: gate Fort Condor + Junon world-map entry on AP key bits
    // via the WorldScriptEditor insert. Self-gates on the seed's free_roam +
    // rules.town_gating (read from the .apff7); a no-op otherwise.
    patchTownGates(lgp);

    // Free Roam: neuter the Diamond Weapon rise/boarding cutscenes DEAD LAST. This MUST
    // run after every re-offsetting wm0.ev editor above (patchHighwindDiamondScene,
    // patchTownGates) — those re-parse + re-assemble the script and would otherwise
    // REVERT these byte-anchor edits (and my edits also interfered with their own gate
    // removal). Anchors are content-based, so the prior re-offsets don't matter.
    m_diamondBoardingPatched = patchDiamondBoardingScene(lgp);

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
             << m_diamondSitesPatched << "Diamond Weapon (field-51) site(s),"
             << m_diamondAmbientPatched << "Diamond Weapon (ambient) site(s),"
             << m_highwindScenePatched << "Diamond Weapon (Highwind scene) site(s),"
             << m_craterLandingPatched << "crater-landing site(s) newly patched)";
    return true;
}
