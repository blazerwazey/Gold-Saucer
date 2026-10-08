#include "EnemyRandomizer.h"

#include "SceneComposer.h"
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

// ═══════════════════════════════════════════════════════════════════════════════

// composeEncounters — rebuild each battle's CAST from enemies drawn game-wide

// ═══════════════════════════════════════════════════════════════════════════════

//

// The sibling of randomizeEncounters, and the opposite trade. The shuffle moves

// whole scenes between slots: every fight it produces is one FF7 already ships,

// but the fight that answers to a formation id changes. Composition leaves every

// scene where it is and swaps the three enemies inside it, so formation ids never

// move - a script that runs BATTLE 468 still starts the fight the chest

// advertises - at the cost of producing casts that have never existed.

//

// See SceneComposer.h for what travels with an enemy and why multi-part sets

// cannot be separated.

bool EnemyRandomizer::composeEncounters()

{

    QString outputPath = m_parent->getOutputPath();

    QString logPath = outputPath + "/encounter_composition_debug.txt";

    QFile logFile(logPath);

    bool logOk = logFile.open(QIODevice::WriteOnly | QIODevice::Text);

    QTextStream dbg(&logFile);

    Q_UNUSED(logOk);

    dbg << "=== Encounter Composition (scene.bin) ===\n\n";

    QString scenePath = QDir(outputPath).filePath("data/lang-en/battle/scene.bin");

    QFile sceneFile(scenePath);

    if (!sceneFile.open(QIODevice::ReadOnly)) {

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

    QVector<SceneEntry> scenes;

    if (!extractScenes(sceneBin, scenes, dbg)) {

        dbg << "ERROR: extractScenes failed\n";

        return false;

    }

    // Held back only where there is a reason to. The list was built for the
    // shuffle, which MOVED scenes and so broke the formation id a script names;
    // this pass never moves an id, which changes what each source buys.
    //
    // Chocobo is not a hold at all any more, it is a PIN. Catching needs a
    // chocobo in the formation, and catching is how racing works - but it does
    // not need the monsters standing beside it to be the vanilla ones, and
    // protection is per-SCENE where a scene is FOUR formations. Holding the
    // scene froze the three ordinary encounters sharing its enemy records: 14
    // scenes, 56 formations, all of it open world-map grass. Reported from play
    // as the Wutai and Mideel grass still being vanilla. The composer now keeps
    // the chocobo in its slot and recasts the rest.
    //
    // Weapon, Arena and BossChest stay held: they are content - you should still
    // fight the actual Weapon, the Battle Square draws from its own table, and a
    // Boss in a Box chest should open onto its boss.
    //
    // Plain Field is NOT held. A scripted fight's id does not move, so its check
    // fires whatever the cast is, and protection is per-SCENE - four formations
    // share three enemy records - so one scripted ambush was freezing up to
    // three ordinary encounters beside it. 61 scenes were held for that reason
    // alone, 44 of them not boss-tier; the Midgar reactor Grunts sit at 28
    // average HP. The fights worth keeping are already held by the BOSS TIER
    // that "Include bosses" governs, so the endgame chain - Safer and Bizarro
    // Sephiroth, Jenova Synthesis, Carry Armor, Schizo, Hojo - stays vanilla
    // through a rule that already existed, with no second threshold to tune.

    QSet<int> held;

    QSet<int> pinned;

    QSet<int> weaponFormations;   // their bodies never donate (see SceneComposer.h)

    int fieldReleased = 0;

    for (int i = 0; i < EnemyProtectedFormations::kProtectedCount; ++i) {

        const int scene = EnemyProtectedFormations::kProtected[i] / 4;

        const EnemyProtectedFormations::Kind kind =
            EnemyProtectedFormations::kProtectedKind[i];

        if (kind == EnemyProtectedFormations::Kind::Field) {

            ++fieldReleased;

            continue;

        }

        if (kind == EnemyProtectedFormations::Kind::Chocobo) {

            pinned.insert(EnemyProtectedFormations::kProtected[i]);

            continue;

        }

        held.insert(scene);

        if (kind == EnemyProtectedFormations::Kind::Weapon)

            weaponFormations.insert(EnemyProtectedFormations::kProtected[i]);

    }

    // A scene protected for BOTH reasons is held: the stronger claim wins, and
    // pinning one body inside a Weapon or Battle Square scene would not make it
    // safe to recast the rest.

    QSet<int> pinnedFormations;

    for (int f : pinned) if (!held.contains(f / 4)) pinnedFormations.insert(f);

    pinned = pinnedFormations;

    QSet<int> pinnedSceneCount;

    for (int f : pinned) pinnedSceneCount.insert(f / 4);

    dbg << "Protected: " << held.size() << " scenes held (Weapons, Battle Square, "

        << "boss chests); " << pinnedSceneCount.size() << " chocobo scenes recast "

        << "AROUND their chocobo; " << fieldReleased

        << " scripted-fight formations released - their ids never move, and the "

        << "boss tier still holds the fights worth keeping\n\n";

    QVector<QByteArray> buf;

    QVector<int> blockOf;

    for (const SceneEntry& e : scenes) {

        buf.append(e.decompressed);

        blockOf.append(e.blockIndex);

    }

    // The size the REBUILD will charge for this scene, including the 4-byte

    // alignment it pads to. Measuring the raw gzip length instead would quietly

    // under-count by up to 3 bytes per scene and hand back blocks that overflow.

    SceneComposer composer(m_rng, [](const QByteArray& scene) {

        const int n = gzipCompress(scene).size();

        return n + ((4 - (n % 4)) % 4);

    });

    // The same toggle the shuffle used, meaning the same thing: off holds the
    // boss tier out of the recast AND out of the donor pool.

    const bool includeBosses = m_parent->m_config.getEncounterBossesIncluded();

    SceneComposer::Result res;

    if (!composer.compose(buf, blockOf, held, pinned, weaponFormations, includeBosses, dbg, &res)) {

        dbg << "ERROR: composition failed\n";

        return false;

    }

    for (int i = 0; i < scenes.size() && i < buf.size(); ++i)

        scenes[i].decompressed = buf[i];

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

    qDebug() << "Encounter composition complete." << res.composed << "casts rebuilt.";

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



