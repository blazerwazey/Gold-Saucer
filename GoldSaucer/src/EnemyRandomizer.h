#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QTextStream>
#include <random>

class Randomizer;

// ═══════════════════════════════════════════════════════════════════════════════
// EnemyRandomizer — properly parses scene.bin's compressed block/scene format
//
// scene.bin layout:
//   - Divided into 0x2000-byte blocks
//   - Block header: 16 × 4-byte pointers (×4 = offset within block)
//   - Each pointer references a gzip-compressed scene
//   - 256 total scenes, each decompresses to 7808 bytes
//
// Decompressed scene (7808 bytes). VERIFIED 2026-09-18 against FF7 Scarlet's
// SceneEditor and by re-emitting all 256 scenes byte-for-byte from these parts
// (scratchpad/scene_model.py). The earlier version of this banner was 8 bytes
// low from the enemy records down (it omitted the model-id array at the top),
// left out the 64-byte attack ID table entirely, and called the tail one
// undifferentiated "AI Data" when it is four separate things:
//   0x0000  Enemy model IDs   (3 × 2 = 6 bytes)
//   0x0006  padding           (2 bytes)
//   0x0008  Battle Setup      (4 × 20 = 80 bytes)
//   0x0058  Camera Data       (4 × 48 = 192 bytes)
//   0x0118  Formations        (4 × 96 = 384 bytes; 6 placements × 16 each)
//   0x0298  Enemy 1           (184 bytes = 32 name + 152 data)
//   0x0350  Enemy 2           (184 bytes)
//   0x0408  Enemy 3           (184 bytes)
//   0x04C0  Attack Data       (32 × 28 = 896 bytes)
//   0x0840  Attack IDs        (32 × 2 = 64 bytes)
//   0x0880  Attack Names      (32 × 32 = 1024 bytes)
//   0x0C80  Formation AI offs (4 × 2 = 8 bytes, relative to 0x0C80)
//   0x0C88  Formation AI      (504 bytes)
//   0x0E80  Enemy AI offsets  (3 × 2 = 6 bytes, relative to 0x0E80)
//   0x0E86  Enemy AI          (4090 bytes, to 0x1E80 = 7808)
//
// An enemy names its attacks by GLOBAL attack id, not by a slot in this scene's
// table (all 2502 ids in the shipped file resolve), and each AI container is
// self-contained - it opens with its own offset table and nothing inside the
// bytecode points outside it. Both facts matter: they are why an enemy can be
// moved to another scene by copying its record and its AI and rewriting one
// u16 offset, with no bytecode fixups.
// ═══════════════════════════════════════════════════════════════════════════════

class EnemyRandomizer
{
public:
    explicit EnemyRandomizer(Randomizer* parent);
    // Shuffle whole encounters between slots of similar difficulty, then scale
    // the arrivals to the difficulty of where they landed. The only entry point:
    // the stats-variance pass was removed 2026-09-18, having sat unreachable
    // since the Initial Commit.
    bool randomizeEncounters();

private:
    Randomizer*    m_parent;
    std::mt19937&  m_rng;

    // ── scene.bin constants ──────────────────────────────────────────────
    static const int BLOCK_SIZE        = 0x2000;  // 8192 bytes per block
    static const int BLOCK_HEADER_SIZE = 64;      // 16 × 4-byte pointers
    static const int SCENE_COUNT       = 256;
    static const int SCENE_SIZE        = 7808;    // 0x1E80 decompressed
    static const int ENEMIES_PER_SCENE = 3;
    static const int ENEMY_RECORD_SIZE = 184;     // 0xB8

    // Enemy data starts at this offset within a decompressed scene
    // 6 (enemy IDs) + 2 (pad) + 80 (setup) + 192 (camera) + 384 (formations) = 0x0298
    static const int ENEMY_DATA_BASE   = 0x0298;

    // Battle setup: 4 records (one per formation), 20 bytes each, from 0x0008.
    // The u16 at +0 is the BATTLE LOCATION - the arena background. Verified on
    // the shipped scene.bin: with this stride the field takes a clean, gapless
    // 0..89 across all 256 scenes (plus 0xFFFF on empty ones), and 71 scenes
    // give their four formations DIFFERENT locations, so it is per formation
    // rather than per scene.
    static const int SETUP_BASE        = 0x0008;
    static const int SETUP_STRIDE      = 20;
    static const int SETUP_COUNT       = 4;
    static const int SETUP_LOCATION    = 0x00;   // u16, battle background id

    // ── offsets within a 184-byte enemy record ───────────────────────────
    static const int ENM_NAME     = 0x00;  // 32 bytes (FF7 text)
    static const int ENM_LEVEL    = 0x20;  // u8
    static const int ENM_SPEED    = 0x21;  // u8
    static const int ENM_LUCK     = 0x22;  // u8
    static const int ENM_EVADE    = 0x23;  // u8
    static const int ENM_STR      = 0x24;  // u8
    static const int ENM_DEF      = 0x25;  // u8
    static const int ENM_MAG      = 0x26;  // u8
    static const int ENM_MDEF     = 0x27;  // u8
    static const int ENM_MP       = 0x9C;  // u16
    static const int ENM_HP       = 0xA4;  // u32
    static const int ENM_EXP      = 0xA8;  // u32
    static const int ENM_GIL      = 0xAC;  // u32

    // Boss detection by HP (no reliable global ID in scene.bin)
    // The tier split for the encounter shuffle: tier 4 never moves, tier 3
    // only among itself. These were duplicated as bare literals in the tier
    // classification until the stats pass was removed.
    static const quint32 BOSS_HP_THRESHOLD     = 10000;
    static const quint32 MINIBOSS_HP_THRESHOLD = 4000;

    // ── internal types ───────────────────────────────────────────────────
    struct SceneEntry {
        int  blockIndex;      // which 0x2000 block this came from
        QByteArray compressed;
        QByteArray decompressed;  // 7808 bytes if valid
    };

    // ── scene extraction / rebuild ───────────────────────────────────────
    bool       extractScenes(const QByteArray& sceneBin,
                             QVector<SceneEntry>& scenes,
                             QTextStream& log);
    QByteArray rebuildSceneBin(const QVector<SceneEntry>& scenes,
                               QTextStream& log);


    // ── difficulty scaling ─────────────────────────────────────────────────────
    // Rescale one scene's enemies toward the difficulty of the slot they now
    // occupy. `ratio` is (average HP the slot originally had) / (average HP the
    // incoming enemies natively have). See kStatScaling.
    void       scaleSceneStats(QByteArray& scene, double ratio);

};
