#include "EnemyRandomizer.h"
#include "EnemyProtectedFormations.h"

#include "Randomizer.h"

#include "Config.h"

#include <ff7tk/data/FF7Text.h>

#include <QFile>

#include <QDir>

#include <QDebug>

#include <QDateTime>

#include <cstring>

#include <algorithm>

#include <cmath>

#include <zlib.h>





// Compress data as gzip using raw zlib (GZIP::compress may silently fail)

static QByteArray gzipCompress(const QByteArray& data)

{

    if (data.isEmpty()) return QByteArray();



    z_stream strm;

    memset(&strm, 0, sizeof(strm));

    // 15 + 16 = gzip wrapper. Level 9 and memLevel 9, NOT the defaults.

    //

    // scene.bin is a fixed grid of 0x2000 blocks and Square packed it tight -

    // one block sits within 4 bytes of full. At Z_DEFAULT_COMPRESSION/memLevel 8

    // our deflate is slightly worse than theirs on 148 of the 256 scenes, so

    // merely recompressing the UNCHANGED file put two blocks over the ceiling

    // before any scene moved. At 9/9 the untouched file fits, fullest block

    // 8188/8192 - which is what gives the shuffle a valid state to preserve.

    if (deflateInit2(&strm, 9, Z_DEFLATED,

                     15 + 16, 9, Z_DEFAULT_STRATEGY) != Z_OK)

        return QByteArray();



    strm.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));

    strm.avail_in = static_cast<uInt>(data.size());



    QByteArray out;

    char buf[8192];

    int ret;

    do {

        strm.next_out  = reinterpret_cast<Bytef*>(buf);

        strm.avail_out = sizeof(buf);

        ret = deflate(&strm, Z_FINISH);

        if (ret == Z_STREAM_ERROR) {

            deflateEnd(&strm);

            return QByteArray();

        }

        out.append(buf, static_cast<int>(sizeof(buf) - strm.avail_out));

    } while (ret != Z_STREAM_END);



    deflateEnd(&strm);

    return out;

}



// Decompress gzip data using raw zlib

static QByteArray gzipDecompress(const QByteArray& data, int expectedSize)

{

    if (data.isEmpty()) return QByteArray();



    z_stream strm;

    memset(&strm, 0, sizeof(strm));

    if (inflateInit2(&strm, 15 + 16) != Z_OK) return QByteArray();



    strm.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));

    strm.avail_in = static_cast<uInt>(data.size());



    QByteArray out(expectedSize, '\0');

    strm.next_out  = reinterpret_cast<Bytef*>(out.data());

    strm.avail_out = static_cast<uInt>(expectedSize);



    int ret = inflate(&strm, Z_FINISH);

    inflateEnd(&strm);



    if (ret != Z_STREAM_END) return QByteArray();

    out.resize(expectedSize - static_cast<int>(strm.avail_out));

    return out;

}



EnemyRandomizer::EnemyRandomizer(Randomizer* parent)

    : m_parent(parent)

    , m_rng(const_cast<std::mt19937&>(parent->m_rng))

{

}






// ============================================================================

// Difficulty scaling for a shuffled encounter

//

// A shuffled scene keeps the PLACE's difficulty, not the monsters'. The ratio is

//     (average HP the slot originally had) / (average HP the arrivals have)

// and each stat moves by ratio raised to its own exponent.

//

// The exponents are FITTED from the shipped scene.bin, not chosen: ordinary

// least squares of log(stat) on log(HP) across all 615 enemies. FF7 carries

// difficulty almost entirely in HP - a monster with 100x the HP has only about

// 4.6x the Strength (100^0.34) and 27x the EXP. Scaling everything linearly

// would turn a modest correction into a difficulty and economy break.

//

// Speed, Luck and Evade are included by request. The fit barely supports it -

// Speed r=0.60, Luck r=0.33, and Evade is flat (b=-0.008, r=-0.02, i.e. vanilla

// relates it to HP not at all) - so Evade borrows Luck's gentle exponent rather

// than a measured one. They are deliberately the softest movers in the table.

struct StatScale {

    int    offset;     // byte offset inside the 184-byte enemy record

    int    width;      // 1, 2 or 4

    double exponent;   // stat *= pow(ratio, exponent)

    const char* name;

};



void EnemyRandomizer::scaleSceneStats(QByteArray& scene, double ratio)

{

    // A static local rather than a file-scope table: the ENM_* offsets are
    // private members, so only a member function can name them, and naming them
    // beats repeating the same hex in a second place.

    static const StatScale kStatScaling[] = {

        { ENM_LEVEL, 1, 0.30, "level"    },   // measured 0.301

        { ENM_SPEED, 1, 0.18, "speed"    },   // measured 0.183  (weak, r=0.60)

        { ENM_LUCK, 1, 0.15, "luck"     },   // measured 0.154  (loose, r=0.33)

        { ENM_EVADE, 1, 0.15, "evade"    },   // CHOSEN: vanilla shows none, matched to luck

        { ENM_STR, 1, 0.34, "strength" },   // measured 0.344  (tightest, r=0.80)

        { ENM_DEF, 1, 0.34, "defense"  },   // measured 0.338

        { ENM_MAG, 1, 0.38, "magic"    },   // measured 0.383

        { ENM_MDEF, 1, 0.34, "m.defense"},   // measured 0.338

        { ENM_MP, 2, 0.38, "mp"       },   // measured 0.381

        { ENM_HP, 4, 1.00, "hp"       },   // the reference the ratio is built from

        { ENM_EXP, 4, 0.69, "exp"      },   // measured 0.690 - reward tracks the fight

        { ENM_GIL, 4, 0.70, "gil"      },   // measured 0.700

    };



    if (scene.size() != SCENE_SIZE) return;

    if (!(ratio > 0.0) || qFuzzyCompare(ratio, 1.0)) return;



    char* base = scene.data();

    for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {

        const int off = ENEMY_DATA_BASE + e * ENEMY_RECORD_SIZE;

        bool empty = true;

        for (int n = 0; n < 32; ++n)

            if (static_cast<quint8>(scene.at(off + n)) != 0xFF) { empty = false; break; }

        if (empty) continue;



        for (const StatScale& st : kStatScaling) {

            const int p = off + st.offset;

            quint32 cur = 0;

            if      (st.width == 1) cur = static_cast<quint8>(scene.at(p));

            else if (st.width == 2) { quint16 v; memcpy(&v, base + p, 2); cur = v; }

            else                    { quint32 v; memcpy(&v, base + p, 4); cur = v; }



            // 0 means "this enemy has none of it" - scaling it would invent a

            // stat the designers deliberately left empty.

            if (cur == 0) continue;



            double scaled = static_cast<double>(cur) * std::pow(ratio, st.exponent);

            const double cap = (st.width == 1) ? 255.0

                             : (st.width == 2) ? 65535.0 : 4294967295.0;

            if (scaled < 1.0)  scaled = 1.0;      // never scale a stat out of existence

            if (scaled > cap)  scaled = cap;

            const quint32 out = static_cast<quint32>(scaled + 0.5);



            if      (st.width == 1) base[p] = static_cast<char>(static_cast<quint8>(out));

            else if (st.width == 2) { quint16 v = static_cast<quint16>(out); memcpy(base + p, &v, 2); }

            else                    { quint32 v = out;                       memcpy(base + p, &v, 4); }

        }

    }

}



// ═══════════════════════════════════════════════════════════════════════════════

// randomizeEncounters — shuffle entire scenes between slots of similar difficulty

// ═══════════════════════════════════════════════════════════════════════════════

bool EnemyRandomizer::randomizeEncounters()

{

    // Paths — read from OUTPUT (copyOriginalFiles already placed it there;

    // stats randomization may have already modified it)

    QString outputPath = m_parent->getOutputPath();

    QString logPath = outputPath + "/encounter_randomization_debug.txt";

    QFile logFile(logPath);

    bool logOk = logFile.open(QIODevice::WriteOnly | QIODevice::Text);

    QTextStream dbg(&logFile);

    Q_UNUSED(logOk);

    QString scenePath = QDir(outputPath).filePath("data/lang-en/battle/scene.bin");
    QFile sceneFile(scenePath);
    
    if (!sceneFile.open(QIODevice::ReadOnly)) {

        // Fall back to original

        scenePath = QDir(m_parent->getFF7Path()).filePath("data/lang-en/battle/scene.bin");

        sceneFile.setFileName(scenePath);

        if (!sceneFile.open(QIODevice::ReadOnly)) {

            dbg << "ERROR: Cannot open scene.bin\n";

            return false;

        }

    }

    QByteArray sceneBin = sceneFile.readAll();

    sceneFile.close();

    dbg << "Loaded scene.bin: " << sceneBin.size() << " bytes\n";



    // Extract

    QVector<SceneEntry> scenes;

    if (!extractScenes(sceneBin, scenes, dbg)) {

        dbg << "ERROR: extractScenes failed\n";

        return false;

    }

    dbg << "Extracted " << scenes.size() << " scenes\n\n";



    // Config

    const Config& config = m_parent->m_config;

    // Tier 4 - the top of the HP heuristic - is the one tier the player can

    // open up, and it stays CLOSED by default.

    //

    // What it does NOT do: drop a boss into a random encounter. The shuffle runs

    // within a tier, so opening tier 4 lets tier-4 scenes swap with each OTHER.

    //

    // Why it is still off by default: after the protected list takes its cut,

    // 16 tier-4 scenes remain, and they are not all what the name suggests. Some

    // are ordinary high-HP randoms (Master Tonberry, Vlakorados, King Behemoth).

    // Others are story bosses the protected list genuinely misses, because that

    // list holds the formation a field script NAMES - and a multi-phase fight is

    // named only at its entry. Hojo is listed at formation 856; Helletic Hojo

    // and Lifeform-Hojo N are the phases the engine walks into by itself, in

    // scenes nothing names. So the tier ceiling is the only thing holding them.

    //

    // EnemyProtectedFormations is unaffected by this either way. That list is a

    // correctness guarantee - the client identifies boss kills by formation id -

    // and is applied whatever this is set to.

    const bool includeBosses = config.getEncounterBossesIncluded();

    dbg << "Include bosses in shuffle: " << (includeBosses ? "YES" : "no") << "\n\n";

    // ── Scenes that must never move ────────────────────────────────────────
    // The Archipelago client identifies boss kills BY FORMATION ID, and field
    // scripts start specific fights by id. A scene holds FOUR consecutive
    // formations (formation id = scene * 4 + slot), so protecting one formation
    // means holding its WHOLE SCENE still - swapping the scene would move all
    // four. See EnemyProtectedFormations.h for where the list comes from.
    QSet<int> protectedScenes;
    for (int i = 0; i < EnemyProtectedFormations::kProtectedCount; ++i)
        protectedScenes.insert(EnemyProtectedFormations::kProtected[i] / 4);
    dbg << "Protected: " << EnemyProtectedFormations::kProtectedCount
        << " formations -> " << protectedScenes.size()
        << " scenes held out of the shuffle\n\n";



    // ── Classify each scene into a difficulty tier ──

    // Tier 0: avg HP < 200       (early game)

    // Tier 1: avg HP 200–1000

    // Tier 2: avg HP 1000–4000

    // Tier 3: avg HP 4000–10000  (minibosses)

    // Tier 4: avg HP >= 10000    (bosses)

    // -1 = empty scene (skip)



    QVector<int> tier(scenes.size(), -1);

    for (int i = 0; i < scenes.size(); ++i) {

        if (scenes[i].decompressed.size() != SCENE_SIZE) continue;



        const char* d = scenes[i].decompressed.constData();

        quint64 totalHP = 0;

        int enemyCount  = 0;



        for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {

            int off = ENEMY_DATA_BASE + e * ENEMY_RECORD_SIZE;

            // Skip empty slots (name all 0xFF)

            bool empty = true;

            for (int n = 0; n < 32; ++n) {

                if (static_cast<quint8>(d[off + n]) != 0xFF) { empty = false; break; }

            }

            if (empty) continue;



            quint32 hp;

            memcpy(&hp, d + off + ENM_HP, 4);

            totalHP += hp;

            ++enemyCount;

        }



        if (enemyCount == 0) continue; // empty scene



        quint32 avgHP = static_cast<quint32>(totalHP / enemyCount);

        if      (avgHP >= BOSS_HP_THRESHOLD)     tier[i] = 4;

        else if (avgHP >= MINIBOSS_HP_THRESHOLD) tier[i] = 3;

        else if (avgHP >= 1000)  tier[i] = 2;

        else if (avgHP >= 200)   tier[i] = 1;

        else                     tier[i] = 0;

    }



    // Log tier counts

    int tierCounts[5] = {};

    for (int t : tier) { if (t >= 0 && t <= 4) ++tierCounts[t]; }

    dbg << "Tier counts: ";

    for (int t = 0; t < 5; ++t)

        dbg << "T" << t << "=" << tierCounts[t] << " ";

    dbg << "\n\n";



    // ── Size budget ───────────────────────────────────────────────────────

    //

    // scene.bin pins every scene to a fixed (block, slot): the scene INDEX is

    // block * 16 + slot, so a scene can never move to another block. Only its

    // DATA moves. The invariant that must hold for every block is therefore

    //

    //     64 (pointer header) + sum(compressed size of its scenes) <= 0x2000

    //

    // The old free shuffle ignored this and dropped a large scene's data into a

    // small scene's slot. Measured on the shipped file that overflowed about 8

    // of the 33 blocks on EVERY seed, worst 3784 bytes past the ceiling - and

    // rebuildSceneBin's fallback then either reverted a slot to vanilla

    // (breaking the permutation) or wrote an end marker into the middle of a

    // block header (truncating that block's scene list).

    //

    // So: cost every scene up front, track each block's running total, and only

    // commit a swap that keeps both blocks inside the ceiling.

    QVector<int> compSize(scenes.size(), 0);

    for (int i = 0; i < scenes.size(); ++i) {

        QByteArray c;

        if (scenes[i].decompressed.size() == SCENE_SIZE) {

            c = gzipCompress(scenes[i].decompressed);

            if (c.isEmpty()) {

                dbg << "ERROR: cannot compress scene " << i

                    << " while costing the size budget\n";

                return false;

            }

        } else {

            c = scenes[i].compressed;

        }

        const int n = c.size();

        compSize[i] = n + ((4 - (n % 4)) % 4);   // 4-byte aligned, as packed

    }

    QMap<int, int> blockUsed;

    for (int i = 0; i < scenes.size(); ++i) {

        const int b = scenes[i].blockIndex;

        blockUsed[b] = blockUsed.value(b, BLOCK_HEADER_SIZE) + compSize[i];

    }

    for (auto it = blockUsed.constBegin(); it != blockUsed.constEnd(); ++it) {

        if (it.value() > BLOCK_SIZE) {

            dbg << "ERROR: block " << it.key() << " is already " << it.value()

                << " / " << BLOCK_SIZE << " bytes BEFORE shuffling - refusing, "

                   "as there is no valid arrangement to preserve.\n";

            return false;

        }

    }

    {

        int fullest = 0, fullestBlock = -1;

        for (auto it = blockUsed.constBegin(); it != blockUsed.constEnd(); ++it)

            if (it.value() > fullest) { fullest = it.value(); fullestBlock = it.key(); }

        dbg << "Size budget OK: fullest block is " << fullestBlock << " at "

            << fullest << " / " << BLOCK_SIZE << "\n\n";

    }

    // Which scene's data currently sits at each index, so the log reports real

    // displacement rather than the number of accepted swaps.

    QVector<int> srcOf(scenes.size());

    for (int i = 0; i < scenes.size(); ++i) srcOf[i] = i;



    // The battle BACKGROUND belongs to the place, not to the monsters.

    //

    // Each scene holds SETUP_COUNT battle-setup records and the u16 at

    // SETUP_LOCATION in each is the arena art. Swapping whole scene blobs drags

    // that field along with the enemies, so after the first working shuffle

    // every fight still used the background of wherever those monsters

    // originally lived - Midgar sewer walls in the middle of a forest.

    // Reported from play 2026-09-18.

    //

    // Snapshot each scene's own locations now and stamp them back after the

    // shuffle, so the enemies travel and the scenery stays put.

    QVector<QVector<quint16>> origLoc(scenes.size());

    for (int i = 0; i < scenes.size(); ++i) {

        origLoc[i].resize(SETUP_COUNT);

        if (scenes[i].decompressed.size() != SCENE_SIZE) continue;

        for (int f = 0; f < SETUP_COUNT; ++f) {

            quint16 v = 0;

            memcpy(&v, scenes[i].decompressed.constData()

                       + SETUP_BASE + f * SETUP_STRIDE + SETUP_LOCATION, 2);

            origLoc[i][f] = v;

        }

    }

    // Pristine copies, so a scene can be put back if the restore tips its block

    // over. Reverting is always safe: the unshuffled file fits by definition.

    QVector<QByteArray> pristine(scenes.size());

    for (int i = 0; i < scenes.size(); ++i)

        pristine[i] = scenes[i].decompressed;



    // Each scene's own average enemy HP, which is what "the difficulty that

    // belongs here" means once its monsters have been replaced.

    QVector<double> origAvgHP(scenes.size(), 0.0);

    for (int i = 0; i < scenes.size(); ++i) {

        if (scenes[i].decompressed.size() != SCENE_SIZE) continue;

        const char* d = scenes[i].decompressed.constData();

        quint64 total = 0; int n = 0;

        for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {

            const int off = ENEMY_DATA_BASE + e * ENEMY_RECORD_SIZE;

            bool empty = true;

            for (int k = 0; k < 32; ++k)

                if (static_cast<quint8>(d[off + k]) != 0xFF) { empty = false; break; }

            if (empty) continue;

            quint32 hp; memcpy(&hp, d + off + ENM_HP, 4);

            total += hp; ++n;

        }

        if (n) origAvgHP[i] = static_cast<double>(total) / n;

    }

    // Swap attempts per scene in a tier. 40 reaches ~94% displacement on the

    // shipped file; past that the curve is flat and it only burns RNG.

    const int kSwapRoundsPerScene = 40;

    // ── Shuffle within each tier ──

    int maxTier = includeBosses ? 4 : 3;

    int totalSwaps = 0;



    for (int t = 0; t <= maxTier; ++t) {

        // Collect indices for this tier

        QVector<int> indices;
        int heldBack = 0;
        for (int i = 0; i < scenes.size(); ++i) {
            if (tier[i] != t) continue;
            if (protectedScenes.contains(i)) { ++heldBack; continue; }
            indices.append(i);
        }
        if (heldBack)

            dbg << "Tier " << t << ": held back " << heldBack

                << " protected scene(s)\n";

        // Name the boss-tier scenes the toggle put into play. With it on, a bug

        // report about a fight going wrong needs this line to be answerable.

        if (t == 4 && !indices.isEmpty()) {

            dbg << "Tier 4 IN PLAY (" << indices.size() << " scenes):";

            for (int i : indices) dbg << ' ' << i;

            dbg << "\n";

        }

        if (indices.size() < 2) continue;



        // Random pairwise swaps, each committed only if it still fits.

        //

        // NOT a Fisher-Yates permutation: that decides the whole arrangement

        // before checking anything, so a single impossible pairing invalidates

        // the result and there is nothing to do but throw it away. Swapping one

        // pair at a time keeps the invariant true at every step, so whatever

        // the loop has done so far is always a valid arrangement.

        //

        // Two cases need no check and are taken for free:

        //   - both scenes in the SAME block: that block's total cannot change

        //   - equal compressed size: no block total moves

        // Those are about a tenth of accepted swaps on the shipped file.

        std::uniform_int_distribution<int> pick(0, indices.size() - 1);

        int attempts = 0, accepted = 0, freebies = 0;

        const int rounds = indices.size() * kSwapRoundsPerScene;

        for (int r = 0; r < rounds; ++r) {

            const int a = pick(m_rng), b2 = pick(m_rng);

            if (a == b2) continue;

            const int i = indices[a], j = indices[b2];

            ++attempts;

            const int bi = scenes[i].blockIndex, bj = scenes[j].blockIndex;

            const int si = compSize[i],          sj = compSize[j];

            const bool freeSwap = (bi == bj) || (si == sj);

            if (!freeSwap) {

                if (blockUsed[bi] - si + sj > BLOCK_SIZE) continue;

                if (blockUsed[bj] - sj + si > BLOCK_SIZE) continue;

                blockUsed[bi] += sj - si;

                blockUsed[bj] += si - sj;

            } else {

                ++freebies;

            }

            scenes[i].decompressed.swap(scenes[j].decompressed);

            std::swap(compSize[i], compSize[j]);

            std::swap(srcOf[i],    srcOf[j]);

            ++accepted;

        }

        int swaps = 0;

        for (int idx : indices)

            if (srcOf[idx] != idx) ++swaps;

        dbg << "Tier " << t << ": " << attempts << " attempts, " << accepted

            << " accepted (" << freebies << " needed no budget check), "

            << swaps << " of " << indices.size() << " scenes displaced\n";

        totalSwaps += swaps;

        // Log the first few moves, naming what now lives there.

        int shown = 0;

        for (int idx : indices) {

            if (srcOf[idx] == idx || shown >= 5) continue;

            QByteArray nameRaw = scenes[idx].decompressed.mid(

                ENEMY_DATA_BASE + ENM_NAME, 32);

            QString name = FF7Text::toPC(nameRaw);

            dbg << "  Scene " << idx << " <- was scene " << srcOf[idx]

                << " (\"" << name << "\")\n";

            ++shown;

        }

    }



    dbg << "\nTotal scenes swapped: " << totalSwaps << "\n";

    if (totalSwaps == 0) {

        dbg << "No encounters shuffled.\n";

        return true;

    }



    // ── Match the arrivals to the neighbourhood ───────────────────────────

    // Tiering only guarantees a swap stayed inside a broad HP band, and the

    // bands are wide: each spans 2.5x to 5.7x, and measured over real shuffles

    // 30% of encounters land more than 2x off what belonged in that slot.

    {

        int scaled = 0; double worst = 1.0;

        for (int i = 0; i < scenes.size(); ++i) {

            if (srcOf[i] == i) continue;                 // never moved

            const double here = origAvgHP[i];

            const double came = origAvgHP[srcOf[i]];

            if (here <= 0.0 || came <= 0.0) continue;    // empty scene

            const double ratio = here / came;

            scaleSceneStats(scenes[i].decompressed, ratio);

            worst = qMax(worst, qMax(ratio, 1.0 / ratio));

            ++scaled;

        }

        dbg << "Scaled " << scaled << " shuffled scene(s) to the difficulty of "

            << "where they landed (largest correction " << QString::number(worst, 'f', 2)

            << "x)\n";

    }



    // ── Put the scenery back ───────────────────────────────────────────────

    // Re-stamp every scene's OWN battle locations over whatever arrived with

    // the swapped-in monsters.

    for (int i = 0; i < scenes.size(); ++i) {

        if (scenes[i].decompressed.size() != SCENE_SIZE) continue;

        for (int f = 0; f < SETUP_COUNT; ++f) {

            const quint16 v = origLoc[i][f];

            memcpy(scenes[i].decompressed.data()

                   + SETUP_BASE + f * SETUP_STRIDE + SETUP_LOCATION, &v, 2);

        }

    }



    // Those bytes changed, so every size the budget was tracking is now stale:

    // measured, a location rewrite moves a scene by -8 to +16 bytes, and about

    // one seed in ten ends up a few bytes past a block ceiling. Recount, then

    // revert scenes in any block that no longer fits. Reverting restores the

    // scene that started there, locations included, and the unshuffled file

    // fits by definition, so the repair always terminates.

    for (int i = 0; i < scenes.size(); ++i) {

        if (scenes[i].decompressed.size() != SCENE_SIZE) continue;

        QByteArray c = gzipCompress(scenes[i].decompressed);

        if (c.isEmpty()) {

            dbg << "ERROR: cannot compress scene " << i << " after restoring "

                   "its battle locations\n";

            return false;

        }

        const int n = c.size();

        compSize[i] = n + ((4 - (n % 4)) % 4);

    }

    blockUsed.clear();

    for (int i = 0; i < scenes.size(); ++i) {

        const int b = scenes[i].blockIndex;

        blockUsed[b] = blockUsed.value(b, BLOCK_HEADER_SIZE) + compSize[i];

    }

    int reverted = 0;

    for (auto it = blockUsed.begin(); it != blockUsed.end(); ++it) {

        while (it.value() > BLOCK_SIZE) {

            // Revert the largest displaced scene in this block first: it frees

            // the most and is the likeliest culprit.

            int victim = -1, victimSize = -1;

            for (int i = 0; i < scenes.size(); ++i) {

                if (scenes[i].blockIndex != it.key()) continue;

                if (srcOf[i] == i) continue;             // already original

                if (compSize[i] > victimSize) { victim = i; victimSize = compSize[i]; }

            }

            if (victim < 0) {

                dbg << "ERROR: block " << it.key() << " is " << it.value()

                    << " / " << BLOCK_SIZE << " with nothing left to revert\n";

                return false;

            }

            scenes[victim].decompressed = pristine[victim];

            QByteArray c = gzipCompress(scenes[victim].decompressed);

            if (c.isEmpty()) {

                dbg << "ERROR: cannot compress reverted scene " << victim << "\n";

                return false;

            }

            const int n = c.size();

            const int newSize = n + ((4 - (n % 4)) % 4);

            it.value() += newSize - compSize[victim];

            compSize[victim] = newSize;

            srcOf[victim] = victim;

            --totalSwaps;

            ++reverted;

            dbg << "  REPAIR: reverted scene " << victim << " in block "

                << it.key() << " to fit after the location restore\n";

        }

    }

    dbg << "Battle locations restored to their own scenes"

        << (reverted ? QString(" (%1 scene(s) reverted to fit)").arg(reverted)

                     : QString(""))

        << "; " << totalSwaps << " scenes still shuffled\n";



    // ── Rebuild and write ──

    QByteArray newSceneBin = rebuildSceneBin(scenes, dbg);

    if (newSceneBin.isEmpty()) {

        dbg << "ERROR: rebuildSceneBin failed\n";

        return false;

    }



    QString dstScene = QDir(outputPath).filePath("data/lang-en/battle/scene.bin");

    QDir().mkpath(QFileInfo(dstScene).path());

    QFile outFile(dstScene);

    if (!outFile.open(QIODevice::WriteOnly)) {

        dbg << "ERROR: Cannot write " << dstScene << "\n";

        return false;

    }

    outFile.write(newSceneBin);

    outFile.close();



    dbg << "SUCCESS: Written " << newSceneBin.size() << " bytes to " << dstScene << "\n";

    qDebug() << "Enemy encounter randomization complete." << totalSwaps << "scenes shuffled.";

    return true;

}



// ═══════════════════════════════════════════════════════════════════════════════

// extractScenes — parse block headers, decompress all 256 gzip scenes

// ═══════════════════════════════════════════════════════════════════════════════



bool EnemyRandomizer::extractScenes(const QByteArray& sceneBin,

                                     QVector<SceneEntry>& scenes,

                                     QTextStream& log)

{

    const int fileSize  = sceneBin.size();

    const int numBlocks = fileSize / BLOCK_SIZE;

    int sceneIndex = 0;



    for (int b = 0; b < numBlocks && sceneIndex < SCENE_COUNT; ++b) {

        int blockStart = b * BLOCK_SIZE;



        // Read 16 pointer slots from the block header

        for (int p = 0; p < 16 && sceneIndex < SCENE_COUNT; ++p) {

            quint32 ptr;

            memcpy(&ptr, sceneBin.constData() + blockStart + p * 4, 4);

            if (ptr == 0xFFFFFFFF) break;   // end-of-block marker



            int sceneOff = blockStart + static_cast<int>(ptr) * 4;



            // Find the end of this compressed blob

            int sceneEnd;

            if (p + 1 < 16) {

                quint32 nextPtr;

                memcpy(&nextPtr, sceneBin.constData() + blockStart + (p + 1) * 4, 4);

                sceneEnd = (nextPtr == 0xFFFFFFFF)

                         ? blockStart + BLOCK_SIZE

                         : blockStart + static_cast<int>(nextPtr) * 4;

            } else {

                sceneEnd = blockStart + BLOCK_SIZE;

            }



            if (sceneOff >= fileSize || sceneEnd > fileSize || sceneEnd <= sceneOff) {

                log << "  WARNING: bad pointer in block " << b << " slot " << p << "\n";

                ++sceneIndex;

                continue;

            }



            SceneEntry entry;

            entry.blockIndex  = b;

            entry.compressed  = sceneBin.mid(sceneOff, sceneEnd - sceneOff);



            // Decompress (each scene should be exactly 7808 bytes)

            entry.decompressed = gzipDecompress(entry.compressed, SCENE_SIZE);

            if (entry.decompressed.size() != SCENE_SIZE) {

                log << "  Scene " << sceneIndex << " decompress failed (got "

                    << entry.decompressed.size() << " bytes)\n";

                entry.decompressed.clear();

            }



            scenes.append(entry);

            ++sceneIndex;

        }

    }



    log << "Blocks parsed: " << numBlocks << ", scenes found: " << scenes.size() << "\n";

    return !scenes.isEmpty();

}



// ═══════════════════════════════════════════════════════════════════════════════

// rebuildSceneBin — recompress scenes, pack into 0x2000-byte blocks

//                   preserves original block-to-scene mapping so the

//                   kernel.bin lookup table remains valid

// ═══════════════════════════════════════════════════════════════════════════════



QByteArray EnemyRandomizer::rebuildSceneBin(const QVector<SceneEntry>& scenes,

                                             QTextStream& log)

{

    // Group scene indices by their original block

    QMap<int, QVector<int>> blockMap;   // blockIndex -> [scene list indices]

    for (int i = 0; i < scenes.size(); ++i)

        blockMap[scenes[i].blockIndex].append(i);



    int maxBlock = 0;

    for (auto it = blockMap.constBegin(); it != blockMap.constEnd(); ++it)

        if (it.key() > maxBlock) maxBlock = it.key();



    // Pre-allocate the entire result buffer filled with 0xFF

    const int totalSize = BLOCK_SIZE * (maxBlock + 1);

    QByteArray result(totalSize, static_cast<char>(0xFF));

    char* buf = result.data();   // single writable pointer for the whole file



    int compressOk = 0, compressFail = 0;



    for (int b = 0; b <= maxBlock; ++b) {

        char* blk = buf + b * BLOCK_SIZE;



        if (!blockMap.contains(b)) {

            for (int s = 0; s < 16; ++s) {

                quint32 end = 0xFFFFFFFF;

                memcpy(blk + s * 4, &end, 4);

            }

            continue;

        }



        const QVector<int>& slotIndices = blockMap[b];

        int dataOffset = BLOCK_HEADER_SIZE;

        int slot = 0;



        for (int si : slotIndices) {

            QByteArray comp;

            if (scenes[si].decompressed.size() == SCENE_SIZE) {

                comp = gzipCompress(scenes[si].decompressed);

                if (comp.isEmpty()) {

                    log << "  WARNING: gzip compress failed for scene " << si << "\n";

                    comp = scenes[si].compressed;

                    ++compressFail;

                } else {

                    ++compressOk;

                }

            } else {

                comp = scenes[si].compressed;

            }



            // Pad to 4-byte alignment

            while (comp.size() % 4 != 0)

                comp.append(static_cast<char>(0xFF));



            // Overflow is now a HARD FAILURE, not something to work around.

            //

            // The shuffle holds a per-block size budget, so reaching here means

            // that budget disagrees with what was actually produced, and the

            // only safe thing is to emit nothing. The two old recoveries were

            // both worse than failing:

            //   - falling back to scenes[si].compressed writes the slot's

            //     ORIGINAL scene, not the shuffled one, so one scene's data

            //     appears twice and another's disappears - no longer a

            //     permutation, and the player silently loses an encounter.

            //   - skipping the slot left the pre-filled 0xFFFFFFFF in its header

            //     entry, which reads as an END MARKER in the middle of the

            //     block and truncates its scene list.

            if (dataOffset + comp.size() > BLOCK_SIZE) {

                log << "  ERROR: block " << b << " overflowed at scene " << si

                    << " (" << dataOffset << " + " << comp.size() << " > "

                    << BLOCK_SIZE << "). The size budget should have prevented "

                       "this; refusing to write a corrupt scene.bin.\n";

                return QByteArray();

            }



            // Write pointer (offset / 4)

            quint32 ptr = static_cast<quint32>(dataOffset / 4);

            memcpy(blk + slot * 4, &ptr, 4);



            // Write compressed scene data directly into the result buffer

            memcpy(blk + dataOffset, comp.constData(), comp.size());



            dataOffset += comp.size();

            ++slot;

        }



        // Fill remaining header slots with end marker

        for (int s = slot; s < 16; ++s) {

            quint32 end = 0xFFFFFFFF;

            memcpy(blk + s * 4, &end, 4);

        }

    }



    log << "Rebuilt " << (maxBlock + 1) << " blocks, total "

        << result.size() << " bytes\n";

    log << "Compress stats: " << compressOk << " ok, " << compressFail << " failed\n";

    return result;

}



