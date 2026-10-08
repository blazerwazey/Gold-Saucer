// ============================================================================
// CraterRoster — the Northern Crater regroup only counts characters you HAVE
//
// las0_8 and las2_1 regroup the party after the crater's forks. Each character
// carries a group variable (Var[13][0x55..0x5C]; Cloud's is 0x54) and the
// director, in one uninterrupted run:
//
//     sets the group of everyone who caught up with Cloud
//     counts them        IFUB group == Cloud's -> INC count, else MMBUD 00 (unavailable)
//     if count > 3       MENU 07 - the "Reform" PHS
//     else               IFUB group == Cloud's -> PRTYP (into the party)
//
// Undelivered characters were counted like anyone else, so the Reform screen
// offered them - reported from play as Tifa, at level 1, never received. Vanilla
// already guards Yuffie and Vincent with IFMEMBQ (is this character AVAILABLE);
// the core cast never needed it. Every group test now gets the same guard, aimed
// at that test's own "not with Cloud" branch.
//
// IFMEMBQ reads the PHS availability mask the Archipelago client maintains: it
// clears undelivered characters everywhere it is allowed to, and the only grants
// in the chain (MMBUD 01 for everyone) are in las0_1 and las4_0, both of which
// it covers. So by the time these two fields count, the mask is the truth - and
// the count is too, which means the Reform PHS only opens with more than three
// REAL characters. Hiding them from the screen instead would have left it open
// with too few to fill, which is the PHS softlock.
//
// Its own file so a standalone harness can run exactly this code against the
// player's real field before a build ships.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QString>

namespace CraterRoster {

int apply(const QString& fieldName, QByteArray& field, QString& log);

}
