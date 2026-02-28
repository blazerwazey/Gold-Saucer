#include "EnemyRandomizer.h"

#include "Randomizer.h"

#include "Config.h"

#include <ff7tk/data/FF7Text.h>

#include <QFile>

#include <QDir>

#include <QDebug>

#include <QDateTime>

#include <cstring>

#include <algorithm>

#include <zlib.h>





// Compress data as gzip using raw zlib (GZIP::compress may silently fail)

static QByteArray gzipCompress(const QByteArray& data)

{

    if (data.isEmpty()) return QByteArray();



    z_stream strm;

    memset(&strm, 0, sizeof(strm));

    // 15 + 16 = gzip wrapper

    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,

                     15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)

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



// ═══════════════════════════════════════════════════════════════════════════════

// randomize — main entry point

// ═══════════════════════════════════════════════════════════════════════════════



bool EnemyRandomizer::randomize()

{

    // Paths

    QString ff7Path    = m_parent->getFF7Path();

    QString outputPath = m_parent->getOutputPath();



    // Debug log

    QString logPath = outputPath + "/enemy_randomization_debug.txt";

    QFile logFile(logPath);

    bool logOk = logFile.open(QIODevice::WriteOnly | QIODevice::Text);

    Q_UNUSED(logOk);

    QTextStream dbg(&logFile);

    dbg << "=== Enemy Randomization (scene.bin) ===\n"

        << QDateTime::currentDateTime().toString() << "\n\n";



    QString srcScene   = QDir(ff7Path).filePath("data/lang-en/battle/scene.bin");

    QString dstScene   = QDir(outputPath).filePath("data/lang-en/battle/scene.bin");



    dbg << "Source: " << srcScene << "\n"

        << "Output: " << dstScene << "\n\n";



    // Load original scene.bin

    QFile srcFile(srcScene);

    if (!srcFile.open(QIODevice::ReadOnly)) {

        dbg << "ERROR: Cannot open source scene.bin\n";

        qDebug() << "EnemyRandomizer: cannot open" << srcScene;

        return false;

    }

    QByteArray sceneBin = srcFile.readAll();

    srcFile.close();

    dbg << "Loaded scene.bin: " << sceneBin.size() << " bytes ("

        << (sceneBin.size() / BLOCK_SIZE) << " blocks)\n";



    // Extract all scenes

    QVector<SceneEntry> scenes;

    if (!extractScenes(sceneBin, scenes, dbg)) {

        dbg << "ERROR: Failed to extract scenes\n";

        return false;

    }

    dbg << "Extracted " << scenes.size() << " scenes\n\n";



    // Config

    const Config& config = m_parent->m_config;

    double variance   = config.getEnemyStatsVariance();

    bool bossProtect  = config.getBossProtectionEnabled();

    int  bossIntensity = config.getBossRandomizationIntensity();

    dbg << "Stats variance : " << variance << "\n"

        << "Boss protection: " << (bossProtect ? "ON" : "OFF") << "\n"

        << "Boss intensity : " << bossIntensity << "%\n\n";



    // Keep a copy of original decompressed data for verification

    QVector<QByteArray> originalDecompressed;

    for (const auto& s : scenes)

        originalDecompressed.append(s.decompressed);



    // Randomize each scene

    int modified = 0;

    for (int i = 0; i < scenes.size(); ++i) {

        if (scenes[i].decompressed.size() != SCENE_SIZE) continue;

        randomizeScene(scenes[i], i, dbg);

        ++modified;

    }

    dbg << "\nScenes randomized: " << modified << " / " << scenes.size() << "\n";



    // Verify that randomization actually changed the data

    int dataChanged = 0;

    for (int i = 0; i < scenes.size(); ++i) {

        if (scenes[i].decompressed != originalDecompressed[i])

            ++dataChanged;

    }

    dbg << "Scenes with changed data: " << dataChanged << " / " << scenes.size() << "\n";



    // Quick gzip compress test on first valid scene

    for (int i = 0; i < scenes.size(); ++i) {

        if (scenes[i].decompressed.size() == SCENE_SIZE) {

            QByteArray testComp = gzipCompress(scenes[i].decompressed);

            dbg << "Compress test: scene " << i << " -> " << testComp.size()

                << " bytes (original compressed: " << scenes[i].compressed.size() << ")\n";

            if (testComp.isEmpty())

                dbg << "  ERROR: gzipCompress returned empty!\n";

            else if (testComp.size() >= 2)

                dbg << "  Header: 0x" << QString::number(static_cast<quint8>(testComp[0]), 16)

                    << " 0x" << QString::number(static_cast<quint8>(testComp[1]), 16) << "\n";

            break;

        }

    }

    dbg << "\n";



    // Rebuild scene.bin (preserves original block-to-scene mapping)

    QByteArray newSceneBin = rebuildSceneBin(scenes, dbg);

    if (newSceneBin.isEmpty()) {

        dbg << "ERROR: Failed to rebuild scene.bin\n";

        return false;

    }

    dbg << "Rebuilt scene.bin: " << newSceneBin.size() << " bytes\n";



    // Verify rebuilt data differs from original

    int rebuildDiffs = 0;

    for (int i = 0; i < qMin(sceneBin.size(), newSceneBin.size()); ++i) {

        if (sceneBin[i] != newSceneBin[i]) ++rebuildDiffs;

    }

    dbg << "Rebuilt vs original: " << rebuildDiffs << " bytes differ\n";

    if (rebuildDiffs == 0) {

        dbg << "  *** BUG: Rebuilt scene.bin is IDENTICAL to original! ***\n";

        // Check first block in detail

        for (int b = 0; b < qMin(1, newSceneBin.size() / BLOCK_SIZE); ++b) {

            int base = b * BLOCK_SIZE;

            int blockDiffs = 0;

            for (int i = 0; i < BLOCK_SIZE && base + i < newSceneBin.size(); ++i) {

                if (sceneBin[base + i] != newSceneBin[base + i]) ++blockDiffs;

            }

            // Read pointer 0 from original and rebuilt

            quint32 origPtr, newPtr;

            memcpy(&origPtr, sceneBin.constData() + base, 4);

            memcpy(&newPtr, newSceneBin.constData() + base, 4);

            dbg << "  Block " << b << ": " << blockDiffs << " diffs, ptr0 orig="

                << origPtr << " new=" << newPtr << "\n";

        }

        // Test: manually compress scene 0 decompressed and compare with what's in rebuilt block 0

        if (!scenes.isEmpty() && scenes[0].decompressed.size() == SCENE_SIZE) {

            QByteArray testComp = gzipCompress(scenes[0].decompressed);

            QByteArray origComp = gzipCompress(originalDecompressed[0]);

            dbg << "  Scene0 modified compress: " << testComp.size() << " bytes\n";

            dbg << "  Scene0 original compress: " << origComp.size() << " bytes\n";

            dbg << "  Scene0 decompressed same as original? "

                << (scenes[0].decompressed == originalDecompressed[0] ? "YES" : "NO") << "\n";

            // Check what's actually at the scene data offset in the rebuilt block

            quint32 ptr0;

            memcpy(&ptr0, newSceneBin.constData(), 4);

            int dataOff = static_cast<int>(ptr0) * 4;

            if (dataOff + 4 <= newSceneBin.size()) {

                dbg << "  Block0 data @" << dataOff << ": "

                    << QString::number(static_cast<quint8>(newSceneBin[dataOff]), 16) << " "

                    << QString::number(static_cast<quint8>(newSceneBin[dataOff+1]), 16) << " "

                    << QString::number(static_cast<quint8>(newSceneBin[dataOff+2]), 16) << " "

                    << QString::number(static_cast<quint8>(newSceneBin[dataOff+3]), 16) << "\n";

            }

        }

    }



    // Write output

    QDir().mkpath(QFileInfo(dstScene).path());

    if (QFile::exists(dstScene))

        QFile::remove(dstScene);

    QFile outFile(dstScene);

    if (!outFile.open(QIODevice::WriteOnly)) {

        dbg << "ERROR: Cannot write output scene.bin\n";

        return false;

    }

    qint64 written = outFile.write(newSceneBin);

    outFile.flush();

    outFile.close();



    dbg << "Write result: " << written << " of " << newSceneBin.size() << " bytes\n";



    // Verify the written file

    QFile verifyFile(dstScene);

    if (verifyFile.open(QIODevice::ReadOnly)) {

        QByteArray verifyData = verifyFile.readAll();

        verifyFile.close();

        int verifyDiffs = 0;

        for (int i = 0; i < qMin(sceneBin.size(), verifyData.size()); ++i) {

            if (sceneBin[i] != verifyData[i]) ++verifyDiffs;

        }

        dbg << "Verify: file on disk has " << verifyDiffs

            << " bytes different from original (size=" << verifyData.size() << ")\n";

    }



    dbg << "SUCCESS: Written to " << dstScene << "\n";

    qDebug() << "Enemy randomization complete." << modified << "scenes modified.";

    return true;

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

    bool includeBosses = config.getEncounterBossesIncluded();

    dbg << "Include bosses in shuffle: " << (includeBosses ? "YES" : "NO") << "\n\n";



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

        if      (avgHP >= 10000) tier[i] = 4;

        else if (avgHP >= 4000)  tier[i] = 3;

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



    // ── Shuffle within each tier ──

    int maxTier = includeBosses ? 4 : 3;

    int totalSwaps = 0;



    for (int t = 0; t <= maxTier; ++t) {

        // Collect indices for this tier

        QVector<int> indices;

        for (int i = 0; i < scenes.size(); ++i) {

            if (tier[i] == t) indices.append(i);

        }

        if (indices.size() < 2) continue;



        // Fisher-Yates shuffle of the decompressed data among these indices

        // Save a copy of all decompressed data for the tier

        QVector<QByteArray> origData;

        for (int idx : indices)

            origData.append(scenes[idx].decompressed);



        // Shuffle the index mapping

        QVector<int> shuffled(indices.size());

        for (int i = 0; i < shuffled.size(); ++i) shuffled[i] = i;

        for (int i = shuffled.size() - 1; i > 0; --i) {

            std::uniform_int_distribution<int> dist(0, i);

            int j = dist(m_rng);

            std::swap(shuffled[i], shuffled[j]);

        }



        // Apply the shuffle

        int swaps = 0;

        for (int i = 0; i < indices.size(); ++i) {

            scenes[indices[i]].decompressed = origData[shuffled[i]];

            if (shuffled[i] != i) ++swaps;

        }

        totalSwaps += swaps;



        dbg << "Tier " << t << ": " << indices.size() << " scenes, "

            << swaps << " swapped\n";



        // Log first few swaps

        for (int i = 0; i < qMin(5, indices.size()); ++i) {

            if (shuffled[i] != i) {

                // Read enemy name from newly assigned data

                QByteArray nameRaw = scenes[indices[i]].decompressed.mid(

                    ENEMY_DATA_BASE + ENM_NAME, 32);

                QString name = FF7Text::toPC(nameRaw);

                dbg << "  Scene " << indices[i] << " <- was scene "

                    << indices[shuffled[i]] << " (\"" << name << "\")\n";

            }

        }

    }



    dbg << "\nTotal scenes swapped: " << totalSwaps << "\n";



    if (totalSwaps == 0) {

        dbg << "No encounters shuffled.\n";

        return true;

    }



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

            entry.slotInBlock = p;

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



            // Overflow check

            if (dataOffset + comp.size() > BLOCK_SIZE) {

                log << "  WARNING: block " << b << " overflow for scene " << si << "\n";

                comp = scenes[si].compressed;

                while (comp.size() % 4 != 0)

                    comp.append(static_cast<char>(0xFF));

                // Double-check: if STILL doesn't fit, skip

                if (dataOffset + comp.size() > BLOCK_SIZE) {

                    log << "  ERROR: block " << b << " scene " << si

                        << " cannot fit even with original data\n";

                    ++slot;

                    continue;

                }

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



// ═══════════════════════════════════════════════════════════════════════════════

// randomizeScene — modify enemy stats within one decompressed 7808-byte scene

// ═══════════════════════════════════════════════════════════════════════════════



void EnemyRandomizer::randomizeScene(SceneEntry& scene, int sceneIndex,

                                      QTextStream& log)

{

    const Config& config = m_parent->m_config;

    double baseVariance  = config.getEnemyStatsVariance();

    bool   bossProtect   = config.getBossProtectionEnabled();

    int    bossIntensity = config.getBossRandomizationIntensity();



    for (int e = 0; e < ENEMIES_PER_SCENE; ++e) {

        int off = ENEMY_DATA_BASE + e * ENEMY_RECORD_SIZE;



        // Skip empty enemy slots (name is all 0xFF)

        bool empty = true;

        for (int n = 0; n < 32; ++n) {

            if (static_cast<quint8>(scene.decompressed.at(off + ENM_NAME + n)) != 0xFF) {

                empty = false;

                break;

            }

        }

        if (empty) continue;



        // Read current HP to classify enemy

        quint32 hp;

        memcpy(&hp, scene.decompressed.constData() + off + ENM_HP, 4);



        // Determine variance (boss protection based on HP)

        double variance = baseVariance;

        QString typeStr = "Normal";

        if (bossProtect && hp >= BOSS_HP_THRESHOLD) {

            variance = baseVariance * 0.15 * (bossIntensity / 100.0);

            typeStr  = "Boss";

        } else if (bossProtect && hp >= MINIBOSS_HP_THRESHOLD) {

            variance = baseVariance * 0.35 * (bossIntensity / 100.0);

            typeStr  = "MiniBoss";

        }



        // Read original stats

        quint8  origLv  = static_cast<quint8>(scene.decompressed.at(off + ENM_LEVEL));

        quint8  origStr = static_cast<quint8>(scene.decompressed.at(off + ENM_STR));

        quint8  origDef = static_cast<quint8>(scene.decompressed.at(off + ENM_DEF));

        quint8  origMag = static_cast<quint8>(scene.decompressed.at(off + ENM_MAG));

        quint8  origMD  = static_cast<quint8>(scene.decompressed.at(off + ENM_MDEF));

        quint16 origMP;

        memcpy(&origMP, scene.decompressed.constData() + off + ENM_MP, 2);

        quint32 origEXP, origGil;

        memcpy(&origEXP, scene.decompressed.constData() + off + ENM_EXP, 4);

        memcpy(&origGil, scene.decompressed.constData() + off + ENM_GIL, 4);



        // Randomize

        char* d = scene.decompressed.data() + off;

        quint8 newLv = randU8(origLv, variance);

        d[ENM_LEVEL] = static_cast<char>(newLv);

        d[ENM_SPEED] = static_cast<char>(randU8(static_cast<quint8>(d[ENM_SPEED]), variance));

        d[ENM_LUCK]  = static_cast<char>(randU8(static_cast<quint8>(d[ENM_LUCK]),  variance));

        d[ENM_EVADE] = static_cast<char>(randU8(static_cast<quint8>(d[ENM_EVADE]), variance));

        quint8 newStr = randU8(origStr, variance);  d[ENM_STR]  = static_cast<char>(newStr);

        quint8 newDef = randU8(origDef, variance);  d[ENM_DEF]  = static_cast<char>(newDef);

        quint8 newMag = randU8(origMag, variance);  d[ENM_MAG]  = static_cast<char>(newMag);

        quint8 newMD  = randU8(origMD,  variance);  d[ENM_MDEF] = static_cast<char>(newMD);



        quint16 newMP  = randU16(origMP, variance);

        memcpy(d + ENM_MP, &newMP, 2);

        quint32 newHP  = randU32(hp,      variance);

        memcpy(d + ENM_HP, &newHP, 4);

        quint32 newEXP = randU32(origEXP, variance);

        memcpy(d + ENM_EXP, &newEXP, 4);

        quint32 newGil = randU32(origGil, variance);

        memcpy(d + ENM_GIL, &newGil, 4);



        // Decode FF7-encoded enemy name for log

        QByteArray nameRaw = scene.decompressed.mid(off + ENM_NAME, 32);

        QString name = FF7Text::toPC(nameRaw);



        log << "S" << sceneIndex << " E" << e

            << " [" << typeStr << "] \"" << name << "\""

            << " Lv:" << origLv << "->" << newLv

            << " HP:" << hp << "->" << newHP

            << " STR:" << origStr << "->" << newStr

            << " DEF:" << origDef << "->" << newDef

            << " MAG:" << origMag << "->" << newMag

            << " MDEF:" << origMD << "->" << newMD

            << " MP:" << origMP << "->" << newMP

            << "\n";

    }

}



// ═══════════════════════════════════════════════════════════════════════════════

// Stat randomization helpers

// ═══════════════════════════════════════════════════════════════════════════════



quint8 EnemyRandomizer::randU8(quint8 base, double variance)

{

    if (base == 0) return 0;

    std::uniform_real_distribution<double> dist(-variance, variance);

    double result = base * (1.0 + dist(m_rng));

    return static_cast<quint8>(std::clamp(static_cast<int>(result), 1, 255));

}



quint16 EnemyRandomizer::randU16(quint16 base, double variance)

{

    if (base == 0) return 0;

    std::uniform_real_distribution<double> dist(-variance, variance);

    double result = base * (1.0 + dist(m_rng));

    return static_cast<quint16>(std::clamp(static_cast<int>(result), 1, 65535));

}



quint32 EnemyRandomizer::randU32(quint32 base, double variance)

{

    if (base == 0) return 0;

    std::uniform_real_distribution<double> dist(-variance, variance);

    double result = base * (1.0 + dist(m_rng));

    return static_cast<quint32>(std::clamp(

        static_cast<qint64>(result), 1LL, 999999LL));

}

