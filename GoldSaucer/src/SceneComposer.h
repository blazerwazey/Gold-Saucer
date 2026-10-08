#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QTextStream>
#include <functional>
#include <random>

// ═══════════════════════════════════════════════════════════════════════════════
// SceneComposer — rebuild a battle's CAST from enemies drawn anywhere in the game
//
// The encounter shuffle moves whole scenes between slots. This does something
// different: it leaves every scene where it is and replaces the three enemies
// living inside it, so a fight can hold monsters that never met in the shipped
// game — a Tonberry, a Materia Keeper and a wolf.
//
// Why this is safe for Archipelago where moving scenes is not: FORMATION IDS
// NEVER CHANGE. A formation id is (scene index × 4 + slot), and the scene index
// is the scene's position in the file, which composition does not touch. A field
// script that runs `BATTLE 468` still starts the fight the Boss in a Box chest
// advertises, and the savemap flag it sets afterwards still fires its check. The
// monsters are different; the identity of the battle is not.
//
// What has to travel with an enemy — the scene layout in EnemyRandomizer.h is
// the reference, and it was verified by round-tripping all 256 shipped scenes:
//
//   the 184-byte record   copied verbatim. An enemy names its attacks by GLOBAL
//                         attack id, not by a slot in its scene's table, so
//                         nothing inside the record needs remapping.
//   the model id          into the scene's model array AND into every formation
//                         placement that referenced the enemy being replaced,
//                         because placements key on MODEL ID, not on slot.
//   its attack rows       data + id + name, into the shared 32-slot table, which
//                         becomes the union over all three enemies. Manipulated
//                         attack ids count: an enemy whose manip attacks are
//                         missing is one you can manipulate into a crash.
//   its AI container      concatenated into the 4090-byte enemy AI block with
//                         one new u16 offset. Containers open with their own
//                         offset table and nothing inside the bytecode points
//                         out of them, so no bytecode is ever rewritten.
//
// Two rules the shipped data forced:
//
//   MULTI-PART ENEMIES MOVE AS A SET, AT THEIR ORIGINAL SLOTS. A part's AI
//   addresses other enemy SLOTS ("if enemy 0 is dead"), so splitting Carry Armor
//   from its arms leaves an arm waiting for a body that is now a wolf — and
//   moving the body from slot 0 to slot 1 breaks it just as thoroughly. Sets are
//   derived from the player's own scene.bin rather than shipped as a table: for
//   each enemy, intersect the casts of every formation it appears in, and
//   whatever survives is something it has never been seen without.
//
//   COMPOSITION MUST FIT THE BLOCK GRID. scene.bin is 33 fixed 0x2000 blocks and
//   the shipped file leaves almost no slack — blocks average 7538 of 8128 usable
//   bytes and the fullest has 19 to spare. Composing without checking overflows
//   22 of 33 blocks. So a cast is drawn, composed, and then committed only if it
//   still fits the hole the old scene left; otherwise another cast is drawn. The
//   file is therefore valid at every step, with no repair pass to get wrong.
//   Scenes are visited tightest-block-first so the fullest blocks get first call
//   on their own slack.
// ═══════════════════════════════════════════════════════════════════════════════

class SceneComposer
{
public:
    // sizeOf must report the COMPRESSED size of a scene under exactly the
    // compressor that will write the file, or the budget maths is fiction.
    SceneComposer(std::mt19937& rng, std::function<int(const QByteArray&)> sizeOf);

    struct Result {
        int eligible   = 0;   // scenes that could have been composed
        int composed   = 0;   // scenes that were
        int leftVanilla = 0;  // no cast fit the block's remaining slack
        int coupledSets = 0;  // multi-part sets found in this scene.bin
        int units      = 0;   // indivisible draws available
        int bossHeld   = 0;   // boss-tier scenes left vanilla
        int bossDonors = 0;   // boss-tier units kept out of the pool
        int multiPartDropped = 0; // real multi-part enemies kept out of the pool
        int pinnedComposed = 0;   // chocobo scenes recast around their chocobo
        int pinnedHeld = 0;       // chocobo scenes held whole after all
        double worstScale = 1.0;  // largest difficulty correction applied
    };

    // `scenes` holds DECOMPRESSED 7808-byte buffers indexed by scene id, and is
    // edited in place. `blockOf` says which 0x2000 block each scene lives in.
    // Scenes in `held` are never touched, nor are empty ones.
    // includeBosses: when false the boss tier is held out BOTH ways - those
    // scenes keep their vanilla cast and their enemies leave the donor pool.
    // The second half matters here in a way it never did for the shuffle, which
    // moves whole scenes within a tier: this mode draws individuals, so without
    // it a grassland fight can hold Safer Sephiroth.
    // `pinFormations` are FORMATION ids (scene x 4 + slot) whose scene should be
    // composed like any other EXCEPT that the enemy it was protected for keeps
    // its slot, its record and its placements.
    //
    // The chocobo case: catching needs a chocobo in the formation, but not the
    // vanilla monsters beside it, and holding the whole scene froze the three
    // ordinary encounters that share its enemy records - 56 formations of open
    // grass, reported from play as Wutai and Mideel still being vanilla.
    //
    // Formation ids rather than scene ids because that is what identifies the
    // enemy to pin: see derivePinnedModels.
    // Every BODY that a `neverDonateFormations` formation fields is kept out of
    // the donor pool wherever it appears, whatever includeBosses says. The
    // Weapons: their AI is written for their own fight - Ultimate's flee ENDS it
    // - so two Ultimates dealt into one formation softlocked the moment one fled,
    // and a pincer layout holding one crashed. By MODEL, not by scene, because
    // the same Ultimate also sits in scene 247 (formation 988), off the list.
    bool compose(QVector<QByteArray>& scenes,
                 const QVector<int>& blockOf,
                 const QSet<int>& held,
                 const QSet<int>& pinFormations,
                 const QSet<int>& neverDonateFormations,
                 bool includeBosses,
                 QTextStream& log,
                 Result* result = nullptr);

private:
    std::mt19937& m_rng;
    std::function<int(const QByteArray&)> m_sizeOf;

    // ── scene.bin layout (see the verified map in EnemyRandomizer.h) ──────
    static const int SCENE_SIZE       = 7808;
    static const int BLOCK_SIZE       = 0x2000;
    static const int BLOCK_HEADER     = 64;
    static const int ENEMIES_PER_SCENE = 3;
    static const int MODEL_IDS        = 0x0000;   // 3 × u16
    static const int FORMATIONS       = 0x0118;   // 4 × 96
    static const int FORMATION_STRIDE = 96;
    static const int FORMATION_COUNT  = 4;
    static const int PLACEMENTS       = 6;        // per formation
    static const int PLACEMENT_SIZE   = 16;
    static const int ENEMY_BASE       = 0x0298;   // 3 × 184
    static const int ENEMY_RECORD     = 184;
    static const int ATK_DATA         = 0x04C0;   // 32 × 28
    static const int ATK_DATA_STRIDE  = 28;
    static const int ATK_IDS          = 0x0840;   // 32 × u16
    static const int ATK_NAMES        = 0x0880;   // 32 × 32
    static const int ATK_NAME_STRIDE  = 32;
    static const int ATTACK_SLOTS     = 32;
    static const int ENM_AI_OFFSETS   = 0x0E80;   // 3 × u16, relative to here
    static const int ENM_AI           = 0x0E86;
    static const int ENM_AI_SIZE      = 4090;

    // within a 184-byte enemy record
    static const int REC_NAME         = 0x00;
    static const int REC_ATTACK_IDS   = 0x48;     // 16 × u16, GLOBAL ids
    static const int REC_MANIP_IDS    = 0x94;     // 3 × u16, also global ids

    // Offsets inside a 184-byte enemy record, for the difficulty scaling.
    static const int ENM_LEVEL = 0x20, ENM_SPEED = 0x21, ENM_LUCK  = 0x22;
    static const int ENM_EVADE = 0x23, ENM_STR   = 0x24, ENM_DEF   = 0x25;
    static const int ENM_MAG   = 0x26, ENM_MDEF  = 0x27, ENM_MP    = 0x9C;
    static const int ENM_HP    = 0xA4, ENM_EXP   = 0xA8, ENM_GIL   = 0xAC;
    // The shuffle's tier-4 line. A scene at or above this average is a boss.
    static const quint32 BOSS_HP_THRESHOLD = 10000;

    static const quint16 NULL16 = 0xFFFF;
    // Offsets in the enemy-AI table count from the table itself, so the first
    // container starts at 3 × 2. Verified: every shipped scene's first offset
    // is exactly 6.
    static const int AI_OFFSET_ORIGIN = ENEMIES_PER_SCENE * 2;

    // One enemy, lifted out of its home scene with everything it needs.
    struct Donor {
        int        scene = -1;
        int        slot  = -1;
        quint16    modelId = NULL16;
        QByteArray record;                 // 184 bytes
        QByteArray ai;                     // its AI container, may be empty
        QVector<quint16> attackIds;        // including manipulated attacks
        QHash<quint16, QByteArray> atkData;   // id -> 28 bytes
        QHash<quint16, QByteArray> atkName;   // id -> 32 bytes
        bool valid = false;
    };

    // One indivisible draw: a lone enemy, or a whole multi-part set that must
    // keep its home slots.
    struct Unit {
        QVector<int>   homeSlots;  // home slot of each member
        QVector<Donor> members;
        bool coupled = false;
    };

    // ── helpers ──────────────────────────────────────────────────────────
    static bool    enemyPresent(const QByteArray& scene, int slot);
    static quint16 readU16(const QByteArray& b, int at);
    static void    writeU16(QByteArray& b, int at, quint16 v);
    static QVector<quint16> recordAttackIds(const QByteArray& record);
    static QString enemyName(const QByteArray& scene, int slot);

    QByteArray aiContainer(const QByteArray& scene, int slot) const;
    Donor      makeDonor(const QVector<QByteArray>& scenes, int scene, int slot) const;

    // model id -> the set it belongs to, for every enemy that has never been
    // seen without a particular companion
    QHash<quint16, int> derivePartSets(const QVector<QByteArray>& scenes,
                                       QVector<QSet<quint16>>& setsOut) const;

    QVector<Unit> buildUnits(const QVector<QByteArray>& scenes,
                             const QHash<quint16, int>& setOf,
                             const QSet<int>& skipScenes,
                             int* multiPartDropped) const;

    // Mean HP of a scene's living enemies; 0 for an empty scene.
    static double sceneAvgHp(const QByteArray& scene);

    // Rescale one scene's enemies toward the difficulty of the slot they now
    // occupy. Moved from the shuffle, exponents and all.
    void scaleSceneStats(QByteArray& scene, double ratio) const;

    // Pick units that tile all three slots. Returns false if it could not.
    // coupledOut says whether a multi-part set was used, which decides whether
    // the formation's bodies may be redealt across the slots.
    // `prefill` holds slots that are already spoken for (a pinned chocobo); the
    // draw fills only what is left and never displaces one.
    bool drawCast(const QVector<Unit>& units, QVector<Donor>& castOut,
                  bool* coupledOut,
                  const QHash<int, Donor>& prefill = QHash<int, Donor>());

    // scene -> the model id to pin in it. A scene is absent when its protected
    // formations do not resolve to exactly one body, and is then held whole.
    QHash<int, quint16> derivePinnedModels(const QVector<QByteArray>& scenes,
                                           const QSet<int>& pinFormations) const;

    // Build the new scene. Returns false (with a reason) when the cast does not
    // fit the 32-slot attack table or the 4090-byte AI block.
    bool composeScene(const QByteArray& dest, const QVector<Donor>& cast,
                      bool castIsCoupled, QByteArray& out, QString* whyNot) const;

    // Every invariant the game relies on, checked on the emitted bytes.
    QStringList validateScene(const QByteArray& scene,
                              const QVector<Donor>& cast) const;
};
