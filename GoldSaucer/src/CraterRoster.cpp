#include "CraterRoster.h"

#include "FieldScriptEditor.h"

#include <QVector>
#include <algorithm>

namespace {

QByteArray hx(const char* s) { return QByteArray::fromHex(s); }

// group variable (bank 13 address) -> character id, read off the vanilla
// MMBUD / PRTYP that each test guards. Aerith (3) has no variable in the chain.
struct Member { quint8 groupVar; quint8 charId; };
const Member kMembers[] = {
    { 0x55, 1 },   // Barret
    { 0x56, 2 },   // Tifa
    { 0x57, 4 },   // Red XIII
    { 0x58, 8 },   // Cid
    { 0x59, 5 },   // Yuffie   (vanilla already IFMEMBQ-guards her count)
    { 0x5A, 6 },   // Cait Sith
    { 0x5C, 7 },   // Vincent  (likewise)
};
const quint8 CLOUD_GROUP = 0x54;

// The main-menu PHS visibility bit the Archipelago client maintains (savemap
// 0x0BC0 bit 8 = Var[1][0x1D] bit 0): set once three characters have been
// received, i.e. four with Cloud. IFUB Var[1][0x1D] bitON 0 - operand by assemble().
const QByteArray kPhsUnlocked = hx("14101d000900");
const QByteArray kMenuPhs     = hx("49000700");
const QByteArray kJmpf        = hx("1000");

bool finish(FieldScriptEditor& ed, QByteArray& field, const QString& tag, QString& log)
{
    QString err;
    const QByteArray out = ed.assemble(err);
    if (out.isEmpty()) { log += tag + "assemble failed - " + err + "\n"; return false; }
    FieldScriptEditor check;
    if (!check.parse(out, err)) { log += tag + "result does not re-parse - " + err + "\n"; return false; }
    field = out;
    return true;
}

// ── las0_8 / las2_1: the regroup counts only characters you have ─────────────
int guardRegroupCount(const QString& f, QByteArray& field, QString& log)
{
    const QString tag = QStringLiteral("  CRATER_ROSTER %1: ").arg(f);
    FieldScriptEditor ed;
    QString err;
    if (!ed.parse(field, err)) { log += tag + "parse failed - " + err + "\n"; return 0; }

    // Every group test: IFUB Var[13][var] == Var[13][0x54]  (14 dd var 54 00 <jmp>)
    struct Site { int idx; quint8 charId; };
    QVector<Site> sites;
    for (const Member& m : kMembers) {
        const QByteArray test = hx("14dd") + char(m.groupVar) + char(CLOUD_GROUP) + char(0x00);
        for (int i = ed.findOpcode(test); i >= 0; i = ed.findOpcode(test, i + 1))
            sites.append({ i, m.charId });
    }
    // Two blocks (count, then PRTYP), one test per member in each.
    const int expected = 2 * int(sizeof(kMembers) / sizeof(kMembers[0]));
    if (sites.size() != expected) {
        log += tag + QStringLiteral("found %1 group test(s), expected %2 - NOT patched\n")
                         .arg(sites.size()).arg(expected);
        return 0;
    }
    // Already patched: the instruction before a test is our IFMEMBQ for it.
    for (const Site& s : sites) {
        const QByteArray prev = ed.instrBytes(s.idx - 1);
        if (prev.size() == 3 && quint8(prev.at(0)) == 0xCC && quint8(prev.at(1)) == s.charId
                && ed.jumpTarget(s.idx - 1) == ed.jumpTarget(s.idx)) {
            log += tag + "already patched\n";
            return 0;
        }
    }

    // Last test first, so the indices of earlier ones do not move under us.
    std::sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) { return a.idx > b.idx; });
    int n = 0;
    for (const Site& s : sites) {
        const int notWithCloud = ed.jumpTarget(s.idx);
        if (notWithCloud < 0) { log += tag + "a group test is not a jump?\n"; return 0; }
        QByteArray guard;
        guard.append(char(0xCC));          // IFMEMBQ
        guard.append(char(s.charId));
        guard.append(char(0x00));          // jump operand, computed by assemble()
        // claimIncoming: Yuffie's and Vincent's vanilla guards jump straight to the
        // NEXT member's test - those jumps must land on that member's guard.
        if (!ed.insertJumpBefore(s.idx, guard, notWithCloud, true, err)) {
            log += tag + "insert guard - " + err + "\n"; return 0;
        }
        ++n;
    }
    if (!finish(ed, field, tag, log)) return 0;
    log += tag + QStringLiteral("%1 group test(s) now also require the character to be "
                                "available (IFMEMBQ)\n").arg(n);
    return n;
}

// ── las0_1: the entrance crew's "pick your party" ────────────────────────────
//
//     crew s4:  ... PRTYE Cloud alone ; WINDOW ; MESSAGE ; WAIT ; MENU 07 (PHS)
//               UC 00 ; MENU2 00 ; RET
//
// The PHS cannot be left with fewer than three in the party, so with fewer than
// three characters it is a softlock - and the PRTYE has just emptied the party.
// While the client's PHS bit is clear (under three received), skip from before
// the PRTYE straight to UC 00: the party the player arrived with is kept, and
// the client has already filled it with everyone they have.
int gateEntranceCrew(QByteArray& field, QString& log)
{
    const QString tag = QStringLiteral("  CRATER_ROSTER las0_1 (entrance crew PHS): ");
    FieldScriptEditor ed;
    QString err;
    if (!ed.parse(field, err)) { log += tag + "parse failed - " + err + "\n"; return 0; }

    // PRTYE Cloud alone; WINDOW 03 ...; MESSAGE 03 19; WAIT 04; MENU 07; UC 00; MENU2 00
    const int prtye = ed.findBytes(hx("ca00fefe"));
    if (prtye < 0) { log += tag + "PRTYE not found - NOT patched\n"; return 0; }
    if (ed.findBytes(hx("ca00fefe"), prtye + 1) >= 0) {
        log += tag + "PRTYE found twice - refusing to guess\n"; return 0;
    }
    if (prtye >= 1 && ed.instrBytes(prtye - 1).left(5) == kPhsUnlocked.left(5)) {
        log += tag + "already patched\n"; return 0;
    }
    if (ed.instrBytes(prtye + 4) != kMenuPhs || ed.instrBytes(prtye + 5) != hx("3300")
            || ed.instrBytes(prtye + 6) != hx("4a00")) {
        log += tag + "not the vanilla PRTYE..MENU 07..UC 00 shape - NOT patched\n"; return 0;
    }
    const int ucOn = prtye + 5;
    if (!ed.insertJumpBefore(prtye, kPhsUnlocked, ucOn, true, err)) {
        log += tag + "insert gate - " + err + "\n"; return 0;
    }
    if (!finish(ed, field, tag, log)) return 0;
    log += tag + "party reset + PHS now only while the main-menu PHS is unlocked\n";
    return 1;
}

// ── las4_0: the reform before the final descent ──────────────────────────────
//
//     dic s3:  ASK ready? ... cutscene ...
//              MENU 07 (PHS)
//              FADE ; REQEW cloud ; PRQEW member#2 s2 ; PRQEW member#1 s2 ; FADE ;
//              MUSIC ; FADEW ; PREQ member#1 s3 ; PRQEW member#2 s4 ; PRQEW member#1 s2 ;
//              JOIN ; REQEW cloud ; RET
//
// Two hazards. PRQEW waits for a party member's script, and never returns when
// that slot is empty - the crater split can leave the party short. And the PHS
// cannot be left with fewer than three, so with under three characters it is a
// softlock.
//
//  * The four member PRQEWs become PREQ (same request, no wait). Their scripts
//    are an empty RET (s2) and a one-second hop (s3/s4), so for a full party the
//    only difference is a JOIN that may start mid-hop. A PREQ to an empty slot
//    does not hang - the Bugenhagen run did exactly that before reaching the
//    campfire.
//  * MENU 07 runs only while the client's PHS bit is set (four or more
//    characters). Otherwise the party is rebuilt without a menu: every
//    available character not already in it is added. The split may have left
//    some of them on another path, and with the main-menu PHS locked there is no
//    other way to get them back. Under four characters there are at most two
//    companions, so this can never overflow the party. Aerith is left out: the
//    crater chain has no model for her.
//
//     GATE:   IFUB PHS-unlocked        -> else REBUILD
//             JMPF -> MENU
//     REBUILD per member c:  IFMEMBQ c -> next ; IFPRTYQ c -> add ; JMPF next ; add: PRTYP c
//             JMPF -> after MENU
//     MENU:   MENU 07 ; (the vanilla tail, waits removed)
int fixFinalReform(QByteArray& field, QString& log)
{
    const QString tag = QStringLiteral("  CRATER_ROSTER las4_0 (final reform): ");
    FieldScriptEditor ed;
    QString err;
    if (!ed.parse(field, err)) { log += tag + "parse failed - " + err + "\n"; return 0; }

    const int menu = ed.findOpcode(kMenuPhs);
    if (menu < 0) { log += tag + "MENU 07 not found - NOT patched\n"; return 0; }
    if (ed.findOpcode(kMenuPhs, menu + 1) >= 0) {
        log += tag + "MENU 07 found twice - refusing to guess\n"; return 0;
    }
    // Already patched: the instruction before MENU 07 is our JMPF over it.
    if (menu >= 1 && quint8(ed.instrBytes(menu - 1).at(0)) == 0x10
            && ed.jumpTarget(menu - 1) == menu + 1) {
        log += tag + "already patched\n"; return 0;
    }

    // The tail's member waits, in order, right after the PHS.
    const char* waits[] = { "0602c2", "0601c2", "060244", "060142" };
    QVector<int> waitIdx;
    int from = menu + 1;
    for (const char* w : waits) {
        int i = -1;
        for (int k = from; k < qMin(menu + 16, ed.instrCount()); ++k)
            if (ed.instrBytes(k) == hx(w)) { i = k; break; }
        if (i < 0) { log += tag + QStringLiteral("member wait %1 not found - NOT patched\n").arg(w); return 0; }
        waitIdx.append(i);
        from = i + 1;
    }
    int n = 0;
    for (int i : waitIdx) {
        QByteArray b = ed.instrBytes(i);
        b[0] = char(0x04);                                   // PRQEW -> PREQ
        if (!ed.replaceAt(i, b, err)) { log += tag + "PRQEW->PREQ - " + err + "\n"; return 0; }
        ++n;
    }

    // Build the gate + rebuild in REVERSE at the MENU's index, so every jump
    // target already exists when its jump is inserted.
    const int m = menu;
    // JMPF -> after MENU  (the FADE that follows MENU 07; pre-insert index m+1)
    if (!ed.insertJumpBefore(m, kJmpf, m + 1, false, err)) {
        log += tag + "insert skip-menu - " + err + "\n"; return 0;
    }
    ++n;
    const quint8 rebuild[] = { 8, 7, 6, 5, 4, 2, 1 };       // reverse of 1,2,4,5,6,7,8
    for (quint8 c : rebuild) {
        // list at m: [X ...] where X is the next block (or the skip-menu JMPF)
        QByteArray prtyp;  prtyp.append(char(0xC8)); prtyp.append(char(c));
        QByteArray ifprty; ifprty.append(char(0xCB)); ifprty.append(char(c)); ifprty.append(char(0));
        QByteArray ifmemb; ifmemb.append(char(0xCC)); ifmemb.append(char(c)); ifmemb.append(char(0));
        if (!ed.insertBefore(m, prtyp, err)) { log += tag + "PRTYP - " + err + "\n"; return 0; }
        // [PRTYP, X]
        if (!ed.insertJumpBefore(m, kJmpf, m + 1, false, err)) { log += tag + "JMPF - " + err + "\n"; return 0; }
        // [JMPF->X, PRTYP, X]
        if (!ed.insertJumpBefore(m, ifprty, m + 1, false, err)) { log += tag + "IFPRTYQ - " + err + "\n"; return 0; }
        // [IFPRTYQ c (not in party -> PRTYP), JMPF->X, PRTYP, X]
        if (!ed.insertJumpBefore(m, ifmemb, m + 3, false, err)) { log += tag + "IFMEMBQ - " + err + "\n"; return 0; }
        // [IFMEMBQ c (unavailable -> X), IFPRTYQ, JMPF, PRTYP, X]
        n += 4;
    }
    const int menuNow = ed.findOpcode(kMenuPhs);
    if (!ed.insertJumpBefore(m, kJmpf, menuNow, false, err)) {   // unlocked -> MENU
        log += tag + "insert to-menu - " + err + "\n"; return 0;
    }
    // GATE: unlocked falls through to the JMPF above; locked -> REBUILD (m+1).
    // claimIncoming: whatever reached the MENU's old slot now meets the gate.
    if (!ed.insertJumpBefore(m, kPhsUnlocked, m + 1, true, err)) {
        log += tag + "insert gate - " + err + "\n"; return 0;
    }
    n += 2;
    if (!finish(ed, field, tag, log)) return 0;
    log += tag + QStringLiteral("%1 edit(s): member waits no longer block; PHS only while "
                                "unlocked, otherwise the party is rebuilt from who is "
                                "available\n").arg(n);
    return n;
}

} // namespace

int CraterRoster::apply(const QString& fieldName, QByteArray& field, QString& log)
{
    const QString f = fieldName.toLower();
    if (f == QLatin1String("las0_8") || f == QLatin1String("las2_1"))
        return guardRegroupCount(f, field, log);
    if (f == QLatin1String("las0_1"))
        return gateEntranceCrew(field, log);
    if (f == QLatin1String("las4_0"))
        return fixFinalReform(field, log);
    return 0;
}
