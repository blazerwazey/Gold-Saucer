#include "PhsGate.h"

#include "FieldScriptEditor.h"

namespace {

// One question that offers the PHS. `ask` is the vanilla ASK opcode exactly;
// `phsLine` is the choice line whose branch runs MENU 0x07.
struct Site {
    const char* field;
    const char* askHex;
    int         phsLine;
    const char* what;
};

// Deliberately a table of verified sites, not a rule applied to every MENU 07.
// 68 vanilla scripts open the PHS directly; nearly all are one-shot story
// events gated on a game moment Free Roam never sits at (fship_3's own `evt1`
// fires only at moment 1199). Each row here was disassembled and checked by
// hand before it went in.
const Site kSites[] = {
    // Highwind Operation Room: the crew member's PHS / Save / Rest question.
    //   ASK   bank5 window 1 message 9, choices lines 1-3 -> Var[5][0]
    //   IFUB  Var[5][0] == 1  -> WAIT, MENU 07 (PHS)
    //   IFUB  Var[5][0] == 2  -> MENU 0E (Save)
    //   IFUB  Var[5][0] == 3  -> fade + rest
    { "fship_3", "48050109010300", 1, "Highwind Operation Room crew" },
};

// PHS main-menu visibility: savemap 0x0BC0 bit 8 = byte 0x0BC1 bit 0, which in
// field-script terms is bank 1 (savemap+0xBA4, 8-bit), address 0x1D, bit 0.
// IFUB Var[1][0x1D] bitON 0  - the jump operand is filled in by assemble().
const QByteArray kPhsVisibleIfub = QByteArray::fromHex("14101d000900");
const QByteArray kJmpf           = QByteArray::fromHex("1000");
const QByteArray kMenuPhs        = QByteArray::fromHex("49000700");

const int ASK_FIRST_LINE = 4;   // byte offset inside the 7-byte ASK
const int ASK_LAST_LINE  = 5;

} // namespace

int PhsGate::apply(const QString& fieldName, QByteArray& field, QString& log)
{
    int gated = 0;
    for (const Site& site : kSites) {
        if (fieldName.compare(QLatin1String(site.field), Qt::CaseInsensitive) != 0)
            continue;
        const QString tag = QStringLiteral("  PHS_GATE %1 (%2): ")
                                .arg(QLatin1String(site.field), QLatin1String(site.what));

        FieldScriptEditor ed;
        QString err;
        if (!ed.parse(field, err)) { log += tag + "parse failed - " + err + "\n"; continue; }

        const QByteArray ask = QByteArray::fromHex(site.askHex);
        const int a = ed.findOpcode(ask);
        if (a < 0) { log += tag + "question not found - NOT gated\n"; continue; }
        if (ed.findOpcode(ask, a + 1) >= 0) {
            log += tag + "question found twice - refusing to guess, NOT gated\n";
            continue;
        }
        // Already gated (the instruction before the ASK is our IFUB)?
        if (a > 0 && ed.instrBytes(a - 1).left(5) == kPhsVisibleIfub.left(5)) {
            log += tag + "already gated\n";
            continue;
        }

        const int first = quint8(ask.at(ASK_FIRST_LINE));
        const int last  = quint8(ask.at(ASK_LAST_LINE));
        // Moving the first choice line past the PHS only removes the PHS if it
        // IS the first choice, and there has to be something left to choose.
        if (first != site.phsLine || first >= last) {
            log += tag + "PHS is not the first of several choices - NOT gated\n";
            continue;
        }
        // The question must actually feed a PHS: MENU 07 within its branch.
        bool opensPhs = false;
        for (int i = a + 1; i < qMin(a + 6, ed.instrCount()); ++i)
            if (ed.instrBytes(i) == kMenuPhs) { opensPhs = true; break; }
        if (!opensPhs) { log += tag + "no MENU 07 after the question - NOT gated\n"; continue; }

        //   before:  ... ASK(lines first..last) ; IFUB answer==1 ...
        //   after:   ... IFUB PHS-visible (false -> LOCKED)
        //                ASK(lines first..last)
        //                JMPF -> DISPATCH
        //     LOCKED:    ASK(lines first+1..last)     ; PHS printed, not selectable
        //     DISPATCH:  IFUB answer==1 ...           ; untouched
        QByteArray locked = ask;
        locked[ASK_FIRST_LINE] = static_cast<char>(first + 1);

        const int dispatch = a + 1;
        if (!ed.insertBefore(dispatch, locked, err)) {       // LOCKED at dispatch
            log += tag + "insert locked question - " + err + "\n"; continue;
        }
        // JMPF at `dispatch`, aimed at the original dispatch (now dispatch + 1)
        if (!ed.insertJumpBefore(dispatch, kJmpf, dispatch + 1, false, err)) {
            log += tag + "insert skip - " + err + "\n"; continue;
        }
        // Guard at `a`, aimed at LOCKED (now dispatch + 1). claimIncoming: any
        // jump into the question now passes through the guard first.
        if (!ed.insertJumpBefore(a, kPhsVisibleIfub, dispatch + 1, true, err)) {
            log += tag + "insert guard - " + err + "\n"; continue;
        }

        const QByteArray out = ed.assemble(err);
        if (out.isEmpty()) { log += tag + "assemble failed - " + err + "\n"; continue; }
        QString rtErr;
        FieldScriptEditor check;
        if (!check.parse(out, rtErr)) {
            log += tag + "patched field does not re-parse - " + rtErr + " - NOT gated\n";
            continue;
        }
        field = out;
        ++gated;
        log += tag + QStringLiteral("gated on the main-menu PHS bit (Var[1][0x1D].0); "
                                    "while locked the question offers lines %1-%2\n")
                         .arg(first + 1).arg(last);
    }
    return gated;
}
