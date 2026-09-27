// ============================================================================
// PhsGate — keep field-script PHS offers behind the same gate as the main menu
//
// The PHS softlocks with a party of fewer than three (reported from play on the
// Highwind, 2026-09-25). The main-menu PHS was already safe: the Archipelago
// client keeps it hidden - menu_visible bit 8, savemap 0x0BC0 - until three
// characters have been received (_PHS_UNLOCK_CHARACTERS). But a field script can
// open the PHS itself with MENU 0x07, and the Highwind Operation Room crew does
// exactly that from a PHS / Save / Rest question, straight past the gate.
//
// Rather than invent a second threshold, each site here is gated on the SAME
// bit, so the two can never disagree and the client stays the one place the
// rule lives. While it is clear, the question is asked with its first choice
// line moved past the PHS line: the option is still printed, but the cursor
// cannot land on it - the "greyed out" the report asked for. The answer is an
// ABSOLUTE line number (checked against 340 vanilla ASKs), so the script's own
// "== 2 -> Save", "== 3 -> Rest" tests keep working unchanged.
//
// Kept in its own file so a standalone harness can run exactly this code
// against the player's real field before a build ships.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QString>

namespace PhsGate {

// Gate every recognised PHS question in `field` (a DECOMPRESSED flevel field).
// Returns how many were gated (0 when this field has none, or when its script
// does not have the exact vanilla shape - a mismatch is logged, never forced).
int apply(const QString& fieldName, QByteArray& field, QString& log);

}
