#ifndef CRATERBARRIERPATCHER_H
#define CRATERBARRIERPATCHER_H

#include <QString>
#include <QByteArray>

/**
 * CraterBarrierPatcher
 *
 * Reactivates the Northern Crater barrier (world-map model 24) and re-gates it
 * on an Archipelago-controlled savemap flag, so the player cannot reach the end
 * of the game until the FF7Client confirms the goal items are collected.
 *
 * Vanilla world_us.lgp / wm0.ev gates each barrier load with:
 *     if Savemap.game_progress < 1580 then load_model(north_crater_barrier)
 * which is always false in Free Roam (game_progress is fixed at 1603), so the
 * barrier never appears. This patcher rewrites that condition to:
 *     if Savemap[0xD27].byte == 0 then load_model(north_crater_barrier)
 * where 0xD27 ("crater_lock") is driven by the runtime client: 0 = locked
 * (barrier shown), 1 = unlocked (barrier gone).
 *
 * The edit is length-preserving (14-byte condition block in both forms), so the
 * LGP is patched in place — no repacking, no offset/ToC changes. It is anchored
 * on the barrier load itself (push_const 24; load_model) and validated against
 * the exact vanilla bytes before writing, so it is idempotent (skips an already
 * patched file) and fails safe on an unexpected layout.
 *
 * Input:  <ff7Path>/data/wm/world_us.lgp   (player-provided game asset)
 * Output: <outputPath>/data/wm/world_us.lgp (patched copy for the mod)
 */
class CraterBarrierPatcher
{
public:
    CraterBarrierPatcher(const QString& ff7Path, const QString& outputPath);

    // Returns true if the patched world_us.lgp was written (including the
    // idempotent case where the file was already patched).
    bool patch();

    int sitesPatched() const { return m_sitesPatched; }

private:
    // Locate wm0.ev (overworld script) within an LGP buffer. Returns false if
    // the archive or the entry cannot be found. On success, dataStart/dataSize
    // describe the wm0.ev payload region within `lgp`.
    bool findWm0(const QByteArray& lgp, int& dataStart, int& dataSize) const;

    // Patch the barrier conditions in `lgp` in place. Returns the number of
    // barrier sites that were newly patched; already-patched sites count as 0.
    // Sets `ok=false` only on a structural error (no sites found at all).
    int patchWorldScript(QByteArray& lgp, bool& ok) const;

    // Neutralize the Free Roam Diamond Weapon spawn in wm0.ev. The overworld
    // "Enter from field 51" handler runs:
    //   if Savemap.vehicle_display.bit[4] then
    //       load_model(Highwind); enter_vehicle(); load_model(Diamond Weapon)
    // which misfires at game moment 1603 (forced Highwind + Diamond Weapon on
    // entry). We rewrite the inner bit test to push_const 0 (always false) so
    // the engine skips the three Entity calls — identical to the vanilla
    // "bit clear" path. Length-preserving (4 bytes), anchored on the unique
    // Highwind+enter_vehicle+Diamond load sequence, validated before writing.
    // Returns 1 if newly patched, 0 if already patched / not found.
    int patchDiamondWeaponSpawn(QByteArray& lgp) const;

    // Neutralize the *ambient* Free Roam Diamond Weapon spawns in wm0.ev. The
    // overworld model loader (System fn @0x434) loads Diamond Weapon (model 10)
    // in two progress blocks gated on:
    //   if Savemap[0xEF6].bit[3] then load_model(Diamond Weapon)
    // 0xEF6.3 is the disc-2 "Diamond is marching on Midgar" story flag, set when
    // the player leaves the Forgotten Capital. Free Roam never wants it, so on
    // each Diamond Weapon (model 10) load that is gated this way we rewrite the
    // bit test to push_const 0 (always false). The 0xEF6.3 bit is read in several
    // other (non-spawn) places, so we anchor on the model-10 load and back-walk
    // the exact "bit 6803 ; goto_if_false ; reset ; load_model 10" shape — only
    // the two ambient spawns match (the field-51/Highwind path is left to
    // patchDiamondWeaponSpawn). Length-preserving (4 bytes), validated, idempotent.
    // Returns the number of sites newly patched.
    int patchDiamondAmbientSpawn(QByteArray& lgp) const;

    // Lower the Northern Crater landing gate in wm0.ev System fn 9 ("crater_landing"):
    //   if Savemap.game_progress >= 1620 then <Highwind descent>
    // Free Roam runs at game moment 1603, so the descent never fires. We rewrite
    // the threshold 1620 -> 1580 (length-preserving, unique anchor, validated).
    // Returns 1 if newly patched, 0 if already patched / not found.
    int patchCraterLanding(QByteArray& lgp) const;

    // Free world-map model budget in Free Roam so Ruby (model 29) / Diamond
    // (model 10) actually render: NOP the decorative Junon cannon (7), Midgar
    // cannon (20) and Rocket Town rocket (15) load_model calls. Stack-neutral
    // (push_const + load_model both removed). Returns the number of loads NOP'd.
    int patchTrimWorldModels(QByteArray& lgp) const;

    static quint32 readU32(const QByteArray& d, int off);

    QString m_ff7Path;
    QString m_outputPath;
    int     m_sitesPatched = 0;
    int     m_diamondSitesPatched = 0;
    int     m_diamondAmbientPatched = 0;
    int     m_craterLandingPatched = 0;
    int     m_modelsTrimmed = 0;
};

#endif // CRATERBARRIERPATCHER_H
