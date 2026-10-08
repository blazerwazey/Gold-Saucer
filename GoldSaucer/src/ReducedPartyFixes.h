// ============================================================================
// ReducedPartyFixes — story scenes that hang with fewer than three in the party
//
// Free Roam lets the player walk around with one or two party members. Vanilla
// never does, so scripts that hand a line to "party member #2" (the THIRD slot)
// with PRQEW - request a script and WAIT for it to finish - wait forever when
// that slot is empty. The Mideel inn (losinn) was the first; this file holds the
// ones found since, each one disassembled and checked by hand before it went in.
//
// Kept out of FieldPickupRandomizer_ff7tk.cpp so a standalone harness can run
// exactly this code against the player's real field before a build ships.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QString>

namespace ReducedPartyFixes {

// Patch `field` (a DECOMPRESSED flevel field) if it is one this file knows.
// Returns the number of edits; a field whose script does not have the exact
// vanilla shape is logged and left alone, never forced.
int apply(const QString& fieldName, QByteArray& field, QString& log);

}
