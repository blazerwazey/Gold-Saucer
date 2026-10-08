#include "ReducedPartyFixes.h"

#include "FieldScriptEditor.h"

namespace {

QByteArray hx(const char* s) { return QByteArray::fromHex(s); }

// cos_btm2 (Cosmo Canyon campfire, night) - the Bugenhagen-death aftermath.
//
// bugin2's BYEBYE sets Var[3][0xBF] bit 7 and MAPJUMPs here. The AD entity's
// Main then runs, for as long as that bit is set:
//
//     IFUB  Var[3][0xBF] & 0x80   -> else the ordinary campfire
//     UC 01 ; MENU2 01            ; control and menu off
//     REQEW CLOUD s7
//     PRQEW party member #2 s9    ; THIRD party slot, and WAITS for it
//     ... Red XIII, member #2, a busy-wait on Var[5][7] ...
//     JOIN 32
//     UC 00 ; MENU2 00            ; control and menu back
//     JMPFL -> RET
//
// With fewer than three in the party the PRQEW never returns. Reported from
// play: "Red XIII watched Bugen die and now Cloud's just chilling at the fire
// forever". Free Roam pins the game moment at 1997, so bugin2 plays its death
// version (gated >= 1610) on every visit, and the flag is never cleared - the
// aftermath would replay at every campfire visit even with a full party.
//
// Fix: a JMPF in front of the aftermath straight to its own epilogue. Control
// and the menu are still handed back by the scene's own UC 00 / MENU2 00, so it
// does not matter whether BYEBYE's MENU2 01 survived the map jump.
int fixCampfireAftermath(QByteArray& field, QString& log)
{
    const QString tag = QStringLiteral("  PARTY_FIX cos_btm2 (Bugenhagen aftermath): ");
    FieldScriptEditor ed;
    QString err;
    if (!ed.parse(field, err)) { log += tag + "parse failed - " + err + "\n"; return 0; }

    // The aftermath's head: UC 01 ; MENU2 01 ; REQEW 05 d7 ; PRQEW 02 c9
    const int head = ed.findBytes(hx("33014a010305d70602c9"));
    if (head < 0) { log += tag + "aftermath not found - NOT patched\n"; return 0; }
    if (ed.findBytes(hx("33014a010305d70602c9"), head + 1) >= 0) {
        log += tag + "aftermath found twice - refusing to guess\n"; return 0;
    }
    // Already patched? Then our JMPF sits between the death-flag test and UC 01.
    if (head >= 2 && quint8(ed.instrBytes(head - 1).at(0)) == 0x10
            && ed.instrBytes(head - 2).left(5) == hx("1430bf8006")) {
        log += tag + "already patched\n"; return 0;
    }
    // ...entered straight from the death-flag test
    if (head < 1 || ed.instrBytes(head - 1).left(5) != hx("1430bf8006")) {
        log += tag + "aftermath is not behind the death-flag test - NOT patched\n"; return 0;
    }
    // ...and ending JOIN 32 ; UC 00 ; MENU2 00 - the epilogue we jump to
    int join = -1;
    for (int i = head; i < qMin(head + 40, ed.instrCount()); ++i)
        if (ed.instrBytes(i) == hx("0832")) { join = i; break; }
    if (join < 0 || ed.instrBytes(join + 1) != hx("3300") || ed.instrBytes(join + 2) != hx("4a00")) {
        log += tag + "epilogue (JOIN; UC 00; MENU2 00) not found - NOT patched\n"; return 0;
    }
    // claimIncoming: anything that reaches the aftermath goes through the skip.
    if (!ed.insertJumpBefore(head, hx("1000"), join + 1, true, err)) {
        log += tag + "insert skip - " + err + "\n"; return 0;
    }
    const QByteArray out = ed.assemble(err);
    if (out.isEmpty()) { log += tag + "assemble failed - " + err + "\n"; return 0; }
    FieldScriptEditor check;
    if (!check.parse(out, err)) { log += tag + "result does not re-parse - " + err + "\n"; return 0; }
    field = out;
    log += tag + "aftermath skipped to its own epilogue (control and menu restored)\n";
    return 1;
}

} // namespace

int ReducedPartyFixes::apply(const QString& fieldName, QByteArray& field, QString& log)
{
    if (fieldName.compare(QLatin1String("cos_btm2"), Qt::CaseInsensitive) == 0)
        return fixCampfireAftermath(field, log);
    return 0;
}
