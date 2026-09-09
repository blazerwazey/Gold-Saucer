#ifndef CRATERBARRIERPATCHER_H
#define CRATERBARRIERPATCHER_H

#include <QString>
#include <QByteArray>
#include <QMap>

/**
 * CraterBarrierPatcher
 *
 * Reactivates the Northern Crater barrier (world-map model 24) and re-gates it
 * on an Archipelago-controlled savemap flag, so the player cannot reach the end
 * of the game until the FF7Client confirms the goal items are collected.
 *
 * Vanilla world_us.lgp / wm0.ev gates each barrier load with:
 *     if Savemap.game_progress < 1580 then load_model(north_crater_barrier)
 * which is always false in Free Roam (game_progress is fixed at 1997), so the
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

    // Path to the loaded .apff7 seed (for reading free_roam + rules.town_gating).
    void setApJsonPath(const QString& p) { m_apJsonPath = p; }

    int sitesPatched() const { return m_sitesPatched; }

private:
    // Locate a named .ev file (wm0.ev/wm2.ev/...) within an LGP buffer. Returns
    // false if the archive or the entry cannot be found. On success,
    // dataStart/dataSize describe the payload region within `lgp`.
    bool findEvFile(const QByteArray& lgp, const char* name, int& dataStart, int& dataSize) const;
    bool findWm0(const QByteArray& lgp, int& dataStart, int& dataSize) const;

    // Patch the barrier conditions in `lgp` in place. Returns the number of
    // barrier sites that were newly patched; already-patched sites count as 0.
    // Sets `ok=false` only on a structural error (no sites found at all).
    int patchWorldScript(QByteArray& lgp, bool& ok) const;

    // Neutralize the Free Roam Diamond Weapon spawn in wm0.ev. The overworld
    // "Enter from field 51" handler runs:
    //   if Savemap.vehicle_display.bit[4] then
    //       load_model(Highwind); enter_vehicle(); load_model(Diamond Weapon)
    // which misfires at game moment 1997 (forced Highwind + Diamond Weapon on
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

    // Neutralize the SECOND Diamond Weapon caller in wm0.ev: the world-map proximity
    // handler (fn 92) that runs diamond_weapon fn 28 (rise + enter_field(highwind_
    // bridge_4)) via `PUSH 29 ; CALL_FN_28` when the player boards the Highwind near
    // the Diamond entity. Neuter the call (CALL_FN_28 20 02 -> RESET 00 01). Length-
    // preserving, unique anchor, idempotent. (patchHighwindDiamondScene handles the
    // other, field-51-gated model-10 caller.)
    int patchDiamondBoardingScene(QByteArray& lgp) const;

    // Reproduce the Diamond Weapon "optional map boss" edits programmatically (via
    // the re-offsetting WorldScriptEditor) so the mod no longer ships a hand-edited
    // world_us.lgp asset. On the player's vanilla wm0.ev this:
    //   * prepends a kill-gate to Diamond's Model functions 0x4a00/0x4a02/0x4a03
    //     (RESET; PUSH_SAVEMAP_BIT 985 (0xC1F.1, weapons_killed.bit[1]);
    //      GOTO_IF_FALSE body; RETURN) so once he is defeated his init/update/touch
    //     do nothing and he stays gone;
    //   * replaces the touch handler's rise-cinematic block (…PUSH 53; PUSH 0;
    //     ENTER_FIELD) with (RESET; PUSH 980; TRIGGER_BATTLE), so touching him on the
    //     overworld starts battle formation 980 instead of the vanilla map jump. The
    //     preceding special[8]∈{0,1,2} collision guard is preserved.
    // Header-anchored (survives recompilation); re-offsets wm0.ev, so it must run
    // with the other WorldScriptEditor passes, before the content-anchored
    // patchDiamondBoardingScene. Returns 1 if applied, 0 if not found / already done.
    int patchDiamondMapBoss(QByteArray& lgp) const;

    // Wrap every RESET;PUSH modelId;LOAD_MODEL site in evName with a
    // PUSH_SAVEMAP_BIT keyBit gate so the entity only loads when the client
    // has set the bit. Ultimate is wm0.ev model 11 bit 7210, Emerald is
    // wm2.ev model 30 bit 7230. Idempotent and fail safe, returns site count.
    int patchWeaponLoadGate(QByteArray& lgp, const char* evName, int modelId,
                            int keyBit, const char* label) const;

    // Neutralize the Highwind-init Diamond Weapon scene in wm0.ev: the Highwind
    // model's init runs a "if last_field_id == 51" block that repositions the
    // Highwind, plays the rise cinematic, and calls diamond_weapon fn 28. We make
    // the comparison impossible (push 51 -> push 0xFFFF) so the block is always
    // skipped. Length-preserving (2 bytes), unique anchor, idempotent.
    int patchHighwindDiamondScene(QByteArray& lgp) const;
    // Make the battle-return Ruby load check her spawn flag 0xF2B.4 instead of 0xF2A.4.
    int patchRubyBattleReturnLoad(QByteArray& lgp) const;
    // WEAPON Arrival roar sound and banner, triggered by client bits 0x405.0-3 (messages 35/36/49/50).
    int patchWeaponArrivalScenes(QByteArray& lgp) const;
    // Make Ultimate Weapon's crater-crash cinematic reachable in Free Roam.
    // highwind_init runs it (call_function(ultima_weapon, 27)) but only inside
    // `if Special.unknown_5 == 1`, and the preceding `if unknown_5 == 0` block
    // ends in a goto that jumps PAST it. Free Roam always enters with
    // unknown_5 == 0, so the crash was unreachable. Two length-preserving
    // constant rewrites via the re-offsetting editor. Idempotent.
    int patchUltimateCrashGate(QByteArray& lgp) const;

    // Keep Ultimate Weapon's MODEL loaded until the crater crash has actually
    // played. The overworld model-loader has two arms, selected by
    // `Special[5] == 0` at wm0 0x058E: block 1 (what Free Roam always runs) loads
    // model 11 only while `!weapons_killed.bit[0]`, so a dead Ultimate is not
    // loaded at all; the crash-scene loader that DOES load him once he is dead
    // lives in block 2 (0x14AA), which Free Roam never reaches. Result:
    // `call_function(ultima_weapon, 27)` fires at an entity that does not exist and
    // silently no-ops — the cinematic's real failure. Fix: in block 1 (and the
    // matching block 3), swap the load gate from `!bit 984` (Ultimate killed) to
    // `!bit 7220` (submarine_flags.bit[4] = crash done), so he stays loaded through
    // the crash and unloads immediately after. 4-byte length-preserving rewrite at
    // exactly 2 sites; idempotent.
    int patchUltimateModelLoad(QByteArray& lgp) const;

    // Lower the Northern Crater landing gate in wm0.ev System fn 9 ("crater_landing"):
    //   if Savemap.game_progress >= 1620 then <Highwind descent>
    // Free Roam runs at game moment 1997, so the descent never fires. We rewrite
    // the threshold 1620 -> 1580 (length-preserving, unique anchor, validated).
    // Returns 1 if newly patched, 0 if already patched / not found.
    int patchCraterLanding(QByteArray& lgp) const;

    // Free Roam town gating: insert, into each gated town's world-map Mesh entry
    // function (just after the movement-mode check, before ENTER_FIELD), a
    // PUSH_SAVEMAP_BIT <key> ; GOTO_IF_FALSE <return> so the field only loads when the
    // AP town key flag is set. Uses the re-offsetting WorldScriptEditor (inserts).
    // Returns the number of towns gated.
    int patchTownGates(QByteArray& lgp) const;

    // Overwrite world messages in-place (length-preserving) in the 'mes' entry of
    // `lgp`. `edits` maps message id -> already-encoded FF7-text blob (with its
    // 0xFF terminator). Keeps numMessages; re-lays the offset table + blobs. Returns
    // false only if the 'mes' entry can't be found or the result won't fit.
    bool overwriteWorldMessages(QByteArray& lgp, const QMap<int, QByteArray>& edits) const;

    // Free Roam welcome banner: overwrite world message id 53 (the "Saving on the
    // World Map" tutorial, shown on first world-map entry) with the FF7 Archipelago
    // welcome text (rainbow "Archipelago"). Returns 1 if applied.
    int patchWelcomeMessage(QByteArray& lgp) const;

    // Encode plain ASCII to FF7 world text (byte = char-0x20). No terminator —
    // callers append control codes / 0xFF.
    static QByteArray encodeWorldText(const QString& s);

    static quint32 readU32(const QByteArray& d, int off);

    QString m_ff7Path;
    QString m_outputPath;
    QString m_apJsonPath;
    int     m_sitesPatched = 0;
    int     m_diamondSitesPatched = 0;
    int     m_diamondAmbientPatched = 0;
    int     m_diamondBoardingPatched = 0;
    int     m_diamondBossPatched = 0;
    int     m_ultimateGatePatched = 0;
    int     m_emeraldGatePatched = 0;
    int     m_highwindScenePatched = 0;
    int     m_craterLandingPatched = 0;
};

#endif // CRATERBARRIERPATCHER_H
