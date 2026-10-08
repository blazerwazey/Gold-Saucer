#include "CraterRoster.h"

#include "FieldScriptEditor.h"

#include <QVector>
#include <algorithm>

namespace {

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

} // namespace

int CraterRoster::apply(const QString& fieldName, QByteArray& field, QString& log)
{
    const QString f = fieldName.toLower();
    if (f != QLatin1String("las0_8") && f != QLatin1String("las2_1")) return 0;
    const QString tag = QStringLiteral("  CRATER_ROSTER %1: ").arg(f);

    FieldScriptEditor ed;
    QString err;
    if (!ed.parse(field, err)) { log += tag + "parse failed - " + err + "\n"; return 0; }

    // Every group test: IFUB Var[13][var] == Var[13][0x54]  (14 dd var 54 00 <jmp>)
    struct Site { int idx; quint8 charId; };
    QVector<Site> sites;
    for (const Member& m : kMembers) {
        const QByteArray test = QByteArray::fromHex("14dd") + char(m.groupVar)
                              + char(CLOUD_GROUP) + char(0x00);
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

    const QByteArray out = ed.assemble(err);
    if (out.isEmpty()) { log += tag + "assemble failed - " + err + "\n"; return 0; }
    FieldScriptEditor check;
    if (!check.parse(out, err)) { log += tag + "result does not re-parse - " + err + "\n"; return 0; }
    field = out;
    log += tag + QStringLiteral("%1 group test(s) now also require the character to be "
                                "available (IFMEMBQ)\n").arg(n);
    return n;
}
