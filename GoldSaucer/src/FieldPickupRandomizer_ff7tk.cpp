#include "ApSeedFile.h"
#include "FieldPickupRandomizer_ff7tk.h"
#include "Randomizer.h"
#include "Config.h"
#include "FieldScriptEditor.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QTextStream>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <LZS>
#include <ff7tk/data/FF7Text.h>
#include <ff7tk/data/FF7Item.h>
#include <algorithm>
#include <array>
#include <limits>
#include <vector>
#include <cstring>
#include <QHash>
#include <QSet>

// Forward decl: NOP all real PMVIE/MOVIE opcodes in a field's section-0 scripts.
// Defined below (after fieldOpcodeLength); used by the md1stin Free Roam handler.
static int nopFieldScriptMovies(QByteArray& d, const QString& fieldName, QTextStream& dbg);

// Forward decl: NOP all SPLIT (0x09) opcodes in a field's scripts (reduced-party
// softlock fix; used by the losinn Free Roam handler).
static int nopFieldScriptSplits(QByteArray& d, const QString& fieldName, QTextStream& dbg);
// Forward decl: reduce one entity script to just its BITON (losinn inn softlock fix).
static int neuterInnGoScript(QByteArray& d, const QString& fieldName,
                             const QByteArray& entityName,
                             quint8 keepAddr, quint8 keepBit,
                             QTextStream& dbg);

namespace {
    constexpr int MOMENT_GAME_START    = 0;
    constexpr int MOMENT_MIDGAR_ESCAPE = 1008; // MainProgress threshold when Shinra HQ changes
    constexpr int MOMENT_FOREVER       = std::numeric_limits<int>::max();
}

// ============================================================================
// Constants
// ============================================================================
const QString FieldPickupRandomizer_ff7tk::DEBUG_FILE_NAME =
    QStringLiteral("field_randomization_debug.txt");

// ============================================================================
// Construction / Destruction
// ============================================================================

FieldPickupRandomizer_ff7tk::FieldPickupRandomizer_ff7tk(Randomizer* parent)
    : QObject(nullptr)
    , m_parent(parent)
    , m_debugMode(true)
{
    m_rng.seed(QDateTime::currentMSecsSinceEpoch());
    qDebug() << "FieldPickupRandomizer_ff7tk: Initialised (Makou-compatible rewrite)";
}

FieldPickupRandomizer_ff7tk::~FieldPickupRandomizer_ff7tk()
{
}

// ============================================================================
// randomize()  –  main entry point from Randomizer::randomizeFieldPickups()
// ============================================================================

bool FieldPickupRandomizer_ff7tk::randomize()
{
    qDebug() << "FieldPickupRandomizer_ff7tk::randomize() called";

    // --- build item pools ---------------------------------------------------
    initializeItemPools();

    // --- locate flevel.lgp --------------------------------------------------
    QString flevelPath = findFlevelPath();
    if (flevelPath.isEmpty()) {
        qDebug() << "ERROR: Could not find flevel.lgp";
        return false;
    }
    qDebug() << "Found flevel.lgp at:" << flevelPath;

    // --- determine output path ----------------------------------------------
    QString outputPath;
    if (m_parent) {
        outputPath = m_parent->getOutputPath();
    }
    if (outputPath.isEmpty()) {
        outputPath = QFileInfo(flevelPath).absolutePath();
    }

    // Make sure output directory structure exists
    QString outputFlevelDir = outputPath + "/data/field";
    QDir().mkpath(outputFlevelDir);
    QString outputFlevel = outputFlevelDir + "/flevel.lgp";

    // --- open LGP using the proven MakouLgpManager --------------------------
    MakouLgpManager lgp;
    if (!lgp.open(flevelPath)) {
        qDebug() << "ERROR: Failed to open LGP:" << lgp.lastError();
        return false;
    }

    QStringList allFiles = lgp.fileList();
    qDebug() << "LGP contains" << allFiles.size() << "files";

    // --- open debug log -----------------------------------------------------
    QString debugPath = outputPath + "/field_randomization_debug.txt";
    QFile debugFile(debugPath);
    bool debugOk = debugFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QTextStream debugStream(&debugFile);
    if (debugOk) {
        debugStream << "=== Field Pickup Randomization ===\n";
        debugStream << "Date      : " << QDateTime::currentDateTime().toString() << "\n";
        debugStream << "Source    : " << flevelPath << "\n";
        debugStream << "Output    : " << outputFlevel << "\n";
        debugStream << "Files     : " << allFiles.size() << "\n\n";
    }

    // --- load Archipelago JSON (AP mode only) --------------------------------
    bool apMode = m_parent && m_parent->m_config.isFeatureEnabled(Config::ArchipelagoIntegration);
    if (apMode) {
        QString apJson = m_parent->m_config.getApJsonPath();
        if (apJson.isEmpty()) {
            if (debugOk) debugStream << "AP JSON: path not configured in config.json (apJsonPath)\n";
            return false;
        }
        if (!loadApJson(apJson, debugStream)) {
            if (debugOk) debugStream << "AP JSON: failed to load " << apJson << "\n";
            return false;
        }
    }

    // --- key item randomization (global pass, before per-file processing) ---
    // Disable key item randomization in AP mode - AP handles all item placement
    bool keyItemEnabled = m_parent && m_parent->m_config.getKeyItemRandomization() && !apMode;
    if (debugOk) {
        debugStream << "Key Item Randomization: "
                    << (keyItemEnabled ? "ENABLED" : "DISABLED");
        if (apMode && m_parent && m_parent->m_config.getKeyItemRandomization()) {
            debugStream << " (disabled in AP mode)";
        }
        debugStream << "\n\n";
    }

    // --- key item placement plan (computed but NOT applied to LGP yet) ------
    QMap<QString, KeyItemFieldMod> keyItemMods;

    if (keyItemEnabled) {
        debugStream << "=== KEY ITEM COLLECTION PASS ===\n";
        QMap<quint32, GlobalKeyItem> uniqueKeyItems;
        QVector<GlobalStitmLocation> globalStitmLocations;

        for (int idx = 0; idx < allFiles.size(); ++idx) {
            const QString& fn = allFiles[idx];
            if (fn.startsWith("blackbg")) continue;
            if (fn == "onna_5") continue; // onna_5 has no key item BITONs but triggers false STITM detections

            QByteArray fd = lgp.fileData(fn);
            if (fd.isEmpty()) continue;
            collectKeyItemsAndStitm(fd, idx, fn, uniqueKeyItems,
                                     globalStitmLocations, debugStream);
        }

        if (!uniqueKeyItems.isEmpty() && !globalStitmLocations.isEmpty()) {
            keyItemMods = performKeyItemSwaps(uniqueKeyItems, globalStitmLocations,
                                              allFiles, debugStream);
        } else {
            debugStream << "No key items or STITM targets found – skipping swap.\n";
        }
        debugStream << "\n";
    }

    // --- process every field file -------------------------------------------
    // Key item byte modifications AND STITM/SMTRA randomization are applied
    // in a single pass per field so nothing gets overwritten.
    int totalModified = 0;
    int filesWithChanges = 0;

    // FieldScriptEditor round-trip self-test: parse+assemble every field with NO
    // edits and assert byte-identical output. This validates the re-offsetter's
    // section/jump model against the player's real flevel before any insert-based
    // patch trusts it. Results go to the debug log.
    // Validated 0-fail across all 684 decodable fields on 2026-06-28; now opt-in
    // via GS_FIELDSCRIPT_SELFTEST to re-check after editor changes.
    const bool fsSelfTest = !qEnvironmentVariableIsEmpty("GS_FIELDSCRIPT_SELFTEST");
    int fsPass = 0, fsFail = 0, fsSkip = 0;
    if (fsSelfTest && debugOk)
        debugStream << "\n=== FieldScriptEditor round-trip self-test ===\n";

    for (const QString& fileName : allFiles) {
        if (fileName.startsWith("blackbg")) continue;
        if (fileName == "onna_5") continue; // Exclude onna_5 from randomization

        QByteArray fieldData = lgp.fileData(fileName);
        if (fieldData.isEmpty()) continue;

        if (fsSelfTest) {
            QByteArray dec = LZS::decompressAllWithHeader(fieldData);
            if (dec.isEmpty()) { fsSkip++; }
            else {
                QString e;
                if (FieldScriptEditor::selfTestRoundTrip(dec, e)) {
                    fsPass++;
                } else {
                    fsFail++;
                    if (debugOk) debugStream << "  FS_SELFTEST FAIL " << fileName << ": " << e << "\n";
                }
            }
        }

        // Check if this field has key item modifications
        const KeyItemFieldMod* kiMod = keyItemMods.contains(fileName)
                                        ? &keyItemMods[fileName] : nullptr;

        bool changed = processFieldFile(fileName, fieldData, debugStream, kiMod);
        if (changed) filesWithChanges++;

        if (!lgp.setFileData(fileName, fieldData)) {
            qDebug() << "WARNING: setFileData failed for" << fileName;
            if (debugOk) debugStream << "WARNING: setFileData failed for "
                                     << fileName << "\n";
        }
    }

    if (fsSelfTest && debugOk)
        debugStream << "=== FieldScriptEditor self-test: " << fsPass << " pass, "
                    << fsFail << " fail, " << fsSkip << " skip (undecodable) ===\n\n";

    // --- key item verification (before save) ---------------------------------
    if (debugOk && keyItemEnabled) {
        debugStream << "\n=== KEY ITEM VERIFICATION (pre-save) ===\n";
        // Pick first placed key item field for verification
        // Re-read from LGP to see final state after all processing
        QStringList verifyFields;
        verifyFields << "blin62_1" << "crcin_1" << "convil_2";
        for (const QString& vf : verifyFields) {
            QByteArray vData = lgp.fileData(vf);
            if (vData.isEmpty()) continue;
            QByteArray vDec = LZS::decompressAllWithHeader(vData);
            if (vDec.isEmpty()) {
                debugStream << "  " << vf << ": decompress failed\n";
                continue;
            }
            debugStream << "  " << vf << ": decompressed " << vDec.size() << " bytes\n";

            // Parse section 0 to find text section
            if (vDec.size() < 42 + 4) continue;
            quint32 sec0off;
            memcpy(&sec0off, vDec.constData() + 6, 4);
            int sec0Data = static_cast<int>(sec0off) + 4;
            if (sec0Data + 8 > vDec.size()) continue;
            quint16 posTexts;
            memcpy(&posTexts, vDec.constData() + sec0Data + 4, 2);
            int textAbsStart = sec0Data + posTexts;

            // Hex dump first 20 bytes of text section
            debugStream << "  TextSection @" << textAbsStart << " first 20 bytes: ";
            for (int i = 0; i < 20 && textAbsStart + i < vDec.size(); ++i) {
                debugStream << QString("%1 ").arg(
                    static_cast<quint8>(vDec.at(textAbsStart + i)), 2, 16, QChar('0'));
            }
            debugStream << "\n";

            // Derive text count
            if (textAbsStart + 4 <= vDec.size()) {
                quint16 firstOff;
                memcpy(&firstOff, vDec.constData() + textAbsStart + 2, 2);
                int tc = firstOff / 2 - 1;
                debugStream << "  firstOff=" << firstOff << " textCount=" << tc << "\n";

                // Read last text entry (should be the newest added)
                if (tc > 0 && tc <= 255) {
                    quint16 lastOff;
                    memcpy(&lastOff, vDec.constData() + textAbsStart + 2 + (tc - 1) * 2, 2);
                    int lastAbsStart = textAbsStart + lastOff;
                    if (lastAbsStart < vDec.size()) {
                        QByteArray lastRaw;
                        for (int i = lastAbsStart; i < vDec.size() && i < lastAbsStart + 60; ++i) {
                            if (static_cast<quint8>(vDec.at(i)) == 0xFF) break;
                            lastRaw.append(vDec.at(i));
                        }
                        debugStream << "  Last text entry [" << (tc - 1) << "] @" << lastOff
                                    << ": \"" << FF7Text::toPC(lastRaw) << "\"\n";
                    }
                }
            }

            // Scan for BITON opcodes in script area to verify key item placement
            int bitonCount = 0;
            quint8 nbEnt = static_cast<quint8>(vDec.at(sec0Data + 2));
            int scriptStart = sec0Data + 32 + 72 * nbEnt;
            int scriptEnd = textAbsStart;
            for (int i = scriptStart; i < scriptEnd - 3; ++i) {
                if (static_cast<quint8>(vDec.at(i)) == 0x82) {
                    quint8 bank = static_cast<quint8>(vDec.at(i + 1));
                    quint8 addr = static_cast<quint8>(vDec.at(i + 2));
                    quint8 bit = static_cast<quint8>(vDec.at(i + 3));
                    // Key item BITONs use bank 1-2, address 0x40-0x46
                    quint8 destBank = (bank >> 4) & 0x0F;
                    if (destBank >= 1 && destBank <= 2 && addr >= 0x40 && addr <= 0x46) {
                        debugStream << "  BITON @" << i << " bank=0x"
                                    << QString::number(bank, 16) << " addr=0x"
                                    << QString::number(addr, 16) << " bit=" << bit << "\n";
                        // Check if there's a MESSAGE within 10 bytes before
                        for (int m = i - 10; m < i; ++m) {
                            if (m >= scriptStart && static_cast<quint8>(vDec.at(m)) == 0x40) {
                                quint8 winId = static_cast<quint8>(vDec.at(m + 1));
                                quint8 txtId = static_cast<quint8>(vDec.at(m + 2));
                                debugStream << "    MESSAGE @" << m << " win=" << winId
                                            << " textID=" << txtId << "\n";
                            }
                        }
                        ++bitonCount;
                    }
                }
            }
            debugStream << "  Key-item BITONs found: " << bitonCount << "\n\n";
        }
    }

    // --- Archipelago verification log -------------------------------------
    if (apMode && !m_apBitonEntries.isEmpty()) {
        writeArchipelagoSidecar(outputPath, debugStream);
    }

    // --- summary ------------------------------------------------------------
    if (debugOk) {
        debugStream << "\n=== Summary ===\n";
        debugStream << "Files with STITM changes: " << filesWithChanges << "\n";
        if (apMode)
            debugStream << "Archipelago BITONs assigned: " << m_apBitonEntries.size() << "\n";
        debugStream << "Session completed: "
                    << QDateTime::currentDateTime().toString() << "\n";
        debugFile.close();
    }

    qDebug() << "Randomization complete. Files modified:" << filesWithChanges;

    // --- save LGP -----------------------------------------------------------
    if (filesWithChanges > 0) {
        if (!lgp.save(outputFlevel)) {
            qDebug() << "ERROR: Failed to save LGP:" << lgp.lastError();
            return false;
        }
        qDebug() << "Saved randomised LGP to:" << outputFlevel;
    } else {
        qDebug() << "No STITM opcodes found – LGP unchanged.";
    }

    lgp.close();
    return true;
}

// ============================================================================
// ff7LzsCompressWithHeader  –  correct FF7 field LZS encoder
//
// ff7tk's bundled LZS::compress is documented as "limited to small data sizes"
// and produces a CORRUPT stream for some large/complex fields (notably convil_2,
// the Fort Condor minigame field) — the recompressed data decompresses to garbage
// and the game crashes when the post-minigame cutscene runs. This is a standard
// Okumura LZSS encoder matching the FF7 field format: 8-unit groups led by a
// control byte (LSB-first; bit=1 literal, bit=0 = 2-byte match), match = 12-bit
// ring position + 4-bit (length-3), ring buffer N=4096 with r starting at N-F
// (4078) and 0x00 init. We encode plain LZSS over the output and map (distance ->
// ring position) with pos = (4078 + outPos - distance) & 4095, which never needs
// the init fill. Output is prefixed with the 4-byte LE compressed-length header.
// Verified by round-trip against LZS::decompressAllWithHeader before use.
// ============================================================================
// Game-compatible FF7 LZS decompressor (Okumura: ring N=4096, r starts at N-F=4078,
// 0x00 init). Matches the GAME's / vanilla decoder. We need our OWN decoder to VERIFY
// recompression: ff7tk's LZS::decompressAllWithHeader agrees with ff7tk's compressor
// (its round-trip always "passes") but ff7tk's compressed output for large fields is
// NOT what the game decodes — so verifying with ff7tk's decoder is useless. Verifying
// with this one catches the game-incompatible output. Input includes the 4-byte LE
// length header.
static QByteArray ff7LzsDecompress(const QByteArray& blob)
{
    if (blob.size() < 4) return QByteArray();
    const unsigned char* p = reinterpret_cast<const unsigned char*>(blob.constData());
    quint32 fsize = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    int n = blob.size() - 4;
    if (static_cast<int>(fsize) < n) n = static_cast<int>(fsize);
    const unsigned char* data = p + 4;
    const int N = 4096;
    unsigned char tb[4096];
    memset(tb, 0, sizeof(tb));
    int r = N - 18;
    QByteArray out;
    int i = 0;
    while (i < n) {
        unsigned char ctrl = data[i++];
        for (int b = 0; b < 8 && i < n; ++b) {
            if (ctrl & 1) {
                unsigned char c = data[i++];
                out.append(char(c)); tb[r] = c; r = (r + 1) & (N - 1);
            } else {
                if (i + 1 >= n) break;
                unsigned char b1 = data[i], b2 = data[i + 1]; i += 2;
                int pos = b1 | ((b2 & 0xF0) << 4);
                int cnt = (b2 & 0x0F) + 3;
                for (int k = 0; k < cnt; ++k) {
                    unsigned char c = tb[(pos + k) & (N - 1)];
                    out.append(char(c)); tb[r] = c; r = (r + 1) & (N - 1);
                }
            }
            ctrl >>= 1;
        }
    }
    return out;
}

static QByteArray ff7LzsCompressWithHeader(const QByteArray& in)
{
    const int n = in.size();
    const unsigned char* d = reinterpret_cast<const unsigned char*>(in.constData());
    const int N = 4096, F = 18, THRESHOLD = 2;

    QHash<quint32, int> head;
    std::vector<int> prevp(n > 0 ? n : 1, -1);
    auto h3 = [&](int p) -> qint64 {
        if (p + 2 >= n) return -1;
        return (quint32(d[p]) << 16) | (quint32(d[p + 1]) << 8) | quint32(d[p + 2]);
    };

    QByteArray out;
    out.reserve(in.size());
    unsigned char ctrl = 0;
    int nbits = 0;
    QByteArray chunk;
    auto flush = [&]() {
        if (nbits == 0) return;
        out.append(char(ctrl));
        out.append(chunk);
        ctrl = 0; nbits = 0; chunk.clear();
    };

    int p = 0;
    while (p < n) {
        int bestLen = 0, bestDist = 0;
        const qint64 hv = h3(p);
        if (hv >= 0) {
            const int minPos = p - N > 0 ? p - N : 0;
            const int maxLen = F < n - p ? F : n - p;
            int cand = head.value(quint32(hv), -1);
            int tries = 0;
            while (cand >= minPos && tries < 128) {
                int l = 0;
                while (l < maxLen && d[cand + l] == d[p + l]) ++l;
                if (l > bestLen) { bestLen = l; bestDist = p - cand; if (l == maxLen) break; }
                cand = prevp[cand]; ++tries;
            }
        }
        if (bestLen > THRESHOLD) {                       // match (>= 3 bytes)
            const int r = (4078 + p) & (N - 1);
            const int pos = (r - bestDist) & (N - 1);
            chunk.append(char(pos & 0xFF));
            chunk.append(char(((pos >> 4) & 0xF0) | ((bestLen - 3) & 0x0F)));
            ++nbits;                                     // control bit stays 0 = match
            for (int k = 0; k < bestLen; ++k) {
                const qint64 hp = h3(p + k);
                if (hp >= 0) { prevp[p + k] = head.value(quint32(hp), -1); head[quint32(hp)] = p + k; }
            }
            p += bestLen;
        } else {                                         // literal
            chunk.append(char(d[p]));
            ctrl |= (1 << nbits);                        // bit = 1 = literal
            ++nbits;
            if (hv >= 0) { prevp[p] = head.value(quint32(hv), -1); head[quint32(hv)] = p; }
            ++p;
        }
        if (nbits == 8) flush();
    }
    flush();

    QByteArray result;
    const quint32 len = quint32(out.size());
    result.append(char(len & 0xFF));
    result.append(char((len >> 8) & 0xFF));
    result.append(char((len >> 16) & 0xFF));
    result.append(char((len >> 24) & 0xFF));
    result.append(out);
    return result;
}

// ============================================================================
// processFieldFile  –  scan, validate, randomise opcodes in one field
// ============================================================================

bool FieldPickupRandomizer_ff7tk::processFieldFile(
    const QString& fieldName,
    QByteArray& fieldData,
    QTextStream& debugStream,
    const KeyItemFieldMod* keyItemMod)
{
    // Field files in flevel.lgp are LZS-compressed with a 4-byte header.
    if (fieldData.size() < 4) return false;

    QByteArray decompressed = LZS::decompressAllWithHeader(fieldData);
    if (decompressed.isEmpty()) {
        debugStream << fieldName << ": LZS decompression failed, skipping\n";
        return false;
    }

    int totalMods = 0;
    QVector<OpcodeModification> modifications;

    // --- Key item modifications (applied BEFORE STITM scan) -----------------
    // This writes BITON opcodes over STITM locations claimed by key items,
    // so the subsequent STITM scan won't find them (0x82 != 0x58).
    if (keyItemMod) {
        // NOP original BITONs (rewrite as harmless BITON targeting unused var)
        for (int off : keyItemMod->bitonNopOffsets) {
            if (off + 3 < decompressed.size()) {
                decompressed[off]     = static_cast<char>(BITON_OPCODE);
                decompressed[off + 1] = static_cast<char>(0x30);  // bank 3 dest, bank 0 src
                decompressed[off + 2] = static_cast<char>(0xFE);  // unused address
                decompressed[off + 3] = static_cast<char>(0x07);  // bit 7
                debugStream << "  NOP original BITON in " << fieldName << " @" << off << "\n";
                totalMods++;
            }
        }

        // Write new BITONs at STITM locations (STITM=5 bytes → BITON=4 + RET=1)
        for (const KeyItemPlacement& p : keyItemMod->placements) {
            int requiredBytes = p.targetIsBiton ? 4 : 5;
            if (p.targetOffset + requiredBytes - 1 < decompressed.size()) {
                // Debug: Show original bytes before replacement
                QString originalBytes;
                for (int i = 0; i < 8 && p.targetOffset + i < decompressed.size(); ++i) {
                    originalBytes += QString("%1 ").arg(static_cast<quint8>(decompressed[p.targetOffset + i]), 2, 16, QChar('0')).toUpper();
                }
                debugStream << "  KEY_ITEM REPLACING @" << p.targetOffset
                            << " original: " << originalBytes << "\n";
                
                decompressed[p.targetOffset]     = static_cast<char>(BITON_OPCODE);
                decompressed[p.targetOffset + 1] = static_cast<char>(p.keyItem.bankByte);
                decompressed[p.targetOffset + 2] = static_cast<char>(p.keyItem.address);
                decompressed[p.targetOffset + 3] = static_cast<char>(p.keyItem.bit);
                if (!p.targetIsBiton)
                    decompressed[p.targetOffset + 4] = static_cast<char>(0x5F); // NOP to pad former STITM slot
                
                // Debug: Show new bytes after replacement
                QString newBytes;
                for (int i = 0; i < 8 && p.targetOffset + i < decompressed.size(); ++i) {
                    newBytes += QString("%1 ").arg(static_cast<quint8>(decompressed[p.targetOffset + i]), 2, 16, QChar('0')).toUpper();
                }
                debugStream << "  KEY_ITEM REPLACED  @" << p.targetOffset
                            << " new:      " << newBytes << "\n";
                debugStream << "  KEY_ITEM BITON @" << p.targetOffset
                            << " -> " << p.keyName
                            << (p.targetIsBiton ? " (existing BITON host)\n" : "\n");
                totalMods++;
            }
            modifications.append(
                OpcodeModification(p.targetOffset,
                                   QString("Key Item: %1").arg(p.keyName),
                                   false));
        }
    }

    // --- Free Roam MAPJUMP injection (must run before STITM scan) -----------
    bool freeRoam = m_parent && m_parent->m_config.getFreeRoam();
    // Free Roam: NOP SPLIT in the Forgotten Capital inn (losinn). SPLIT positions
    // the non-leader party members to fixed sleep coords and blocks until they
    // arrive; a single-character party has empty slots that never arrive ->
    // softlock. See nopFieldScriptSplits.
    if (freeRoam && fieldName.toLower() == "losinn") {
        nopFieldScriptSplits(decompressed, fieldName, debugStream);
        // Forgotten Capital inn: the line-trigger entity "line4" runs the sleep
        // cutscene (its "S4 - Go" script), which hangs a solo party on PRQEW/PREQ
        // to character #2 (@2160/@2163). Reduce that script to just its var write
        // (BITON Var[3][132].3 = addr 0x84 bit 3 = the inn "cutscene done" flag),
        // dropping the blocking party-member execs + the SPLIT.
        neuterInnGoScript(decompressed, fieldName, QByteArray("line4"), 0x84, 3, debugStream);
        // ROOT CAUSE of the world-map Diamond Weapon cutscene: losinn writes
        // GameMoment = 664 (SETWORD bank2[0] = 664, 81 20 00 98 02) — a disc-2 story
        // value. In Free Roam that slams the moment (~1997) down to 664, which ARMS the
        // Highwind-acquisition / Diamond-rise cutscene, so touching the Highwind after
        // leaving the Forgotten City plays "Diamond rises from the ocean". NOP the write
        // (0x5F x5) so the moment stays high and that gate never passes. Same class as
        // the seto1/gidun_3/cos_btm GameMoment writes.
        const int gm = decompressed.indexOf(QByteArray::fromHex("8120009802"));
        if (gm >= 0) {
            for (int j = 0; j < 5; ++j) decompressed[gm + j] = char(0x5f);
            ++totalMods;
            debugStream << "  LOSINN: NOP'd GameMoment=664 write @" << gm << "\n";
        } else {
            debugStream << "  LOSINN: GameMoment=664 anchor not found\n";
        }
    }
    if (freeRoam && fieldName.toLower() == "ujunon2") {
        // Junon beach: entering the screen auto-fires Priscilla's "Did you drown?"
        // dialog from her (invisible-in-Free-Roam) model. Cosmetic — the player can
        // still leave — but jarring. The MESSAGE opcode (0x40 win 0x00 msg 0x1D) that
        // shows message #29 appears exactly once; NOP its 3 bytes (0x5F) so no box
        // opens. Length-preserving; unique 11-byte anchor guards against a false hit.
        const QByteArray anchor = QByteArray::fromHex("5800a2000140001d680000");
        const int a = decompressed.indexOf(anchor);
        if (a >= 0 && decompressed.indexOf(anchor, a + 1) < 0) {
            const int msg = a + 5;   // the 0x40 MESSAGE opcode within the anchor
            for (int j = 0; j < 3; ++j) decompressed[msg + j] = char(0x5f);
            ++totalMods;
            debugStream << "  UJUNON2: NOP'd Priscilla drown MESSAGE @" << msg << "\n";
        } else {
            debugStream << "  UJUNON2: drown MESSAGE anchor not found/ambiguous\n";
        }
    }
    if (freeRoam && fieldName.toLower() == "fship_3") {
        // Highwind operations room: the rest-crew NPC's talk script branches on
        // Var[3][0x16] bit 6 ("Cloud recovered" story bit, never set in Free
        // Roam) — bit OFF shows only "Oh...oh...{Cloud}..." (msg 8) and jumps to
        // script end, skipping the rest/PHS/save service menu. Flip the IFUB
        // oper bitOFF(0x0a) -> bitON(0x09) so Free Roam takes the normal branch
        // (msg 7 + ASK service menu) instead — same one-byte flip as the Ester
        // talk-gate in crcin_1.
        const int ch = decompressed.indexOf(QByteArray::fromHex("143016060a10"));
        if (ch >= 0) {
            decompressed[ch + 4] = char(0x09);
            ++totalMods;
            debugStream << "  FSHIP_3: crew talk-gate flipped (bitOFF->bitON) @" << ch << "\n";
        } else if (decompressed.indexOf(QByteArray::fromHex("143016060910")) >= 0) {
            debugStream << "  FSHIP_3: crew talk-gate already flipped\n";
        } else {
            debugStream << "  FSHIP_3: crew talk-gate anchor not found\n";
        }
        // Latent hazard (same class as the losinn GameMoment=664 bug): earlier
        // story branches in the same script SETWORD GameMoment to 1033 / 1110,
        // which would slam Free Roam's ~1997 down and re-lock moment gates.
        // NOP both writes (0x5F x5).
        for (const char* hex : { "8120000904", "8120005604" }) {
            const int gm = decompressed.indexOf(QByteArray::fromHex(hex));
            if (gm >= 0) {
                for (int j = 0; j < 5; ++j) decompressed[gm + j] = char(0x5f);
                ++totalMods;
                debugStream << "  FSHIP_3: NOP'd GameMoment write (" << hex << ") @" << gm << "\n";
            }
        }
    }
    if (freeRoam && fieldName.toLower() == "semkin_7") {
        // Underwater Reactor dock: the sub-boarding cutscene BITONs three session
        // flags (V[15][0x86].7 + V[15][0x85].5/.4) whose only purpose is the
        // post-boarding dock lockdown — on re-entry the doors (jp_lock), guards
        // and the Carry Armor chest all read them and disable themselves (the
        // reporter's "chest locked, can't advance"). Vanilla never returns here;
        // Free Roam does. NOP all three BITONs (0x5F x4) so the lockdown never
        // arms and the dock stays fully explorable after getting the sub.
        for (const char* hex : { "82f08607", "82f08505", "82f08504" }) {
            const QByteArray pat = QByteArray::fromHex(hex);
            const int a = decompressed.indexOf(pat);
            if (a >= 0 && decompressed.indexOf(pat, a + 1) < 0) {
                for (int j = 0; j < 4; ++j) decompressed[a + j] = char(0x5f);
                ++totalMods;
                debugStream << "  SEMKIN_7: NOP'd dock-lockdown BITON (" << hex << ") @" << a << "\n";
            } else {
                debugStream << "  SEMKIN_7: lockdown BITON " << hex << " not found/ambiguous\n";
            }
        }
        // Leviathan Scales chest (2026-07-15, final): the firing BITON is
        // 82 f0 8d 00. ENGINE BANK TRUTH (disassembled from ff7_en.exe's script
        // var-resolve jump table @0x60fa49/0x60fa6d): script nibble 1/2->0xBA4,
        // 3/4->0xCA4, 5/6->TEMP(0xCC14D0, not savemap!), B/C->0xDA4, D/E->0xEA4,
        // 7/F->0xFA4, 8/9/A invalid, and NOTHING maps to 0x10A4. So nibble F =
        // savemap 0xFA4+0x8D = 0x1031 (== the client's bank-13 base), and the
        // earlier attempt's 82 b0 8d 02 actually wrote 0xDA4+0x8D = 0xE31 — a
        // byte nobody watches (nibble B != client bank 11; GS's (bank&0xF)<<4
        // translation is wrong for banks 5+). Correct minimal patch: KEEP the
        // vanilla nibble, change only the bit -> 82 f0 8d 02 = 0x1031 bit 2 =
        // location 200336's biton (bank 13 / addr 0x8D / bit 2 in
        // locations.json). Possession (0x1031.0) is no longer set by the chest —
        // it comes from Archipelago (the client sets it on item receipt).
        {
            const QByteArray from = QByteArray::fromHex("82f08d00");
            const QByteArray to   = QByteArray::fromHex("82f08d02");
            const int a = decompressed.indexOf(from);
            if (a >= 0 && decompressed.indexOf(from, a + 1) < 0) {
                decompressed.replace(a, 4, to);
                ++totalMods;
                debugStream << "  SEMKIN_7: repointed Leviathan chest firing BITON "
                               "(82f08d00 -> 82f08d02 = 0x1031.2) @" << a << "\n";
            } else {
                debugStream << "  SEMKIN_7: Leviathan chest firing BITON not found/ambiguous\n";
            }
        }
        // ALSO repoint the chest's two open-state gates (live flag map of the
        // loaded field): the IFUBs at the chest handler test the SAME possession
        // bit V[F][0x8D].0 (`14 f0 8d 00 ..`). Since the repointed chest no
        // longer sets that bit — but the CLIENT sets it when it delivers
        // Leviathan Scales — receiving the AP item before opening the chest would
        // make the chest read "already opened" and the check permanently
        // missable. Repoint both gates to the detection bit (0x1031.2) so the
        // chest's opened state tracks the AP check itself: unopened until the
        // check fires, opened afterwards, indifferent to item receipt. Only the
        // bit operand byte changes; comparison + jump bytes are kept.
        {
            const QByteArray gfrom = QByteArray::fromHex("14f08d00");
            const QByteArray gto   = QByteArray::fromHex("14f08d02");
            int n = 0;
            for (int a = decompressed.indexOf(gfrom); a >= 0;
                 a = decompressed.indexOf(gfrom, a + 1)) {
                decompressed.replace(a, 4, gto);
                ++n; ++totalMods;
                debugStream << "  SEMKIN_7: repointed Leviathan chest open-gate IFUB "
                               "(14f08d00 -> 14f08d02) @" << a << "\n";
            }
            if (n != 2)
                debugStream << "  SEMKIN_7: WARNING expected 2 chest open-gate IFUBs, patched "
                            << n << "\n";
        }
    }
    if (freeRoam && fieldName.toLower() == "del1") {
        // Costa del Sol harbor: the town exit MAPJUMPs into del12 — a cutscene
        // variant of the same screen that replays the Rufus/Heidegger post-ship
        // scene in Free Roam before forwarding to del2. Retarget the exit to
        // del2 DIRECTLY, using the exact coords/triangle/direction del12's own
        // forward-jump uses, so the scene field is bypassed entirely. Same
        // 10-byte MAPJUMP redirect as bugin1b/semkin_7. (del12 is only ever
        // entered from this one jump, so nothing else is affected.)
        const QByteArray from = QByteArray::fromHex("60ba0100000000000000"); // MAPJUMP del12 (0,0)
        const QByteArray to   = QByteArray::fromHex("60bb01bafaa6fd820078"); // MAPJUMP del2 @del12's coords
        const int a = decompressed.indexOf(from);
        if (a >= 0 && decompressed.indexOf(from, a + 1) < 0) {
            decompressed.replace(a, to.size(), to);
            ++totalMods;
            debugStream << "  DEL1: harbor exit redirected del12 -> del2 (skip Rufus scene) @" << a << "\n";
        } else if (decompressed.indexOf(to) >= 0) {
            debugStream << "  DEL1: harbor exit already redirected\n";
        } else {
            debugStream << "  DEL1: del12 MAPJUMP anchor not found/ambiguous\n";
        }
    }
    if (freeRoam && fieldName.toLower() == "junin4") {
        // Junon underwater path: two watchmen (mihari/mihari2) + a dog guard the
        // backup RED SUBMARINE. All three show ONLY while Savemap[0xEF4].bit3
        // ("submarine owned") is OFF (IFUB V[d0][0x50].3 bitOFF -> show) and hide
        // once you own it. Free Roam never sets that bit (the Submarine is AP-
        // only), so they show forever and the dog's battle lets you steal the sub.
        //
        // Desired Free Roam behaviour: HIDE the two watchmen, but KEEP the dog
        // visible + solid so it walls off the (non-check) red sub. So:
        //  (a) flip only the watchmen's show-gates bitOFF(0x0a)->bitON(0x09) — they
        //      now show only when the sub is owned = never, so they stay hidden
        //      (Init leaves them invisible/non-solid). Each gate is uniquely keyed
        //      by its jump byte (mihari 0x51, mihari2 0x1d; the dog's is 0x1a, left
        //      untouched so the dog keeps showing).
        //  (b) NOP the dog's on-contact BATTLE + "dog defeated" flag write
        //      (70 00 fb 02 = BATTLE 763 ; 82 30 ec 07 = BITON V[3][0xEC].7) so the
        //      dog can't be fought through — it stays a permanent solid barrier.
        int done = 0;
        for (const char* hx : { "14d050030a51", "14d050030a1d" }) {   // mihari, mihari2
            const int g = decompressed.indexOf(QByteArray::fromHex(hx));
            if (g >= 0) { decompressed[g + 4] = char(0x09); ++done; }
        }
        if (done) {
            ++totalMods;
            debugStream << "  JUNIN4: hid " << done << " watchmen (show-gate bitOFF->bitON)\n";
        } else {
            debugStream << "  JUNIN4: watchmen show-gate anchors not found\n";
        }
        const int b = decompressed.indexOf(QByteArray::fromHex("7000fb028230ec07"));
        if (b >= 0 && decompressed.indexOf(QByteArray::fromHex("7000fb028230ec07"), b + 1) < 0) {
            for (int j = 0; j < 8; ++j) decompressed[b + j] = char(0x5f);
            ++totalMods;
            debugStream << "  JUNIN4: NOP'd dog BATTLE + defeat flag (impassable) @" << b << "\n";
        } else {
            debugStream << "  JUNIN4: dog battle anchor not found/ambiguous\n";
        }
    }
    if (freeRoam && fieldName.toLower() == "subin_1b") {
        // Submarine dock: the vanilla "you got the sub" grant — even on the
        // failure path (steal the red sub) — is two adjacent BITONs on a border
        // (line-trigger) entity that set Savemap[0xEF4] bit4 + bit3 ("gray
        // submarine owned", the flag the client keys off to load the sub model).
        // In Free Roam the Submarine must be AP-only, so NOP both writes (8 bytes
        // -> 0x5F). The AP Submarine item still grants it (client writes 0xEF4.3
        // + 0xEF6.2 on delivery). Unique 8-byte anchor. (semkin_5, the Reno /
        // Carry Armor room, is unrelated — an earlier patch wrongly hit it.)
        const int at = decompressed.indexOf(QByteArray::fromHex("82d0500482d05003"));
        if (at >= 0 && decompressed.indexOf(QByteArray::fromHex("82d0500482d05003"), at + 1) < 0) {
            for (int j = 0; j < 8; ++j) decompressed[at + j] = char(0x5f);
            ++totalMods;
            debugStream << "  SUBIN_1B: NOP'd vanilla submarine-owned grant (0xEF4.4+.3) @" << at << "\n";
        } else {
            debugStream << "  SUBIN_1B: submarine-grant anchor not found/ambiguous\n";
        }
    }
    if (freeRoam && fieldName.toLower() == "losin2") {
        // Forgotten City altar approach writes GameMoment=677 (mid Aerith death
        // sequence) — entering from the rock-climb descent slams Free Roam's
        // ~1997 down and ARMS the death sequence (reported). Same class as the
        // losinn 664 write. NOP (0x5F x5).
        const int gm = decompressed.indexOf(QByteArray::fromHex("812000a502"));
        if (gm >= 0) {
            for (int j = 0; j < 5; ++j) decompressed[gm + j] = char(0x5f);
            ++totalMods;
            debugStream << "  LOSIN2: NOP'd GameMoment=677 write @" << gm << "\n";
        } else {
            debugStream << "  LOSIN2: GameMoment=677 anchor not found\n";
        }
    }
    if (freeRoam && fieldName.toLower() == "loslake1") {
        // The lake writes GameMoment 1392/1398/1399 (disc-3 Bugenhagen return
        // sequence) — visiting drops Free Roam's ~1997 and arms mid-sequence
        // scenes. NOP all three writes (0x5F x5 each).
        for (const char* hex : { "8120007005", "8120007605", "8120007705" }) {
            const int gm = decompressed.indexOf(QByteArray::fromHex(hex));
            if (gm >= 0) {
                for (int j = 0; j < 5; ++j) decompressed[gm + j] = char(0x5f);
                ++totalMods;
                debugStream << "  LOSLAKE1: NOP'd GameMoment write (" << hex << ") @" << gm << "\n";
            }
        }
    }
    if (freeRoam && fieldName.toLower() == "md1stin") {
        if (injectFreeRoamMapJump(decompressed, fieldName, debugStream))
            totalMods++;
        // NOP the opening movie (PMVIE "Set next movie" + MOVIE "Play movie").
        // On disc 3 (forced in Free Roam) it resolves to the placeholder "No53",
        // which crashes on play — the same failure mode as the Fort Condor No33
        // movie, and the cause of the Kalm-inn crash players hit when the intro
        // cutscene isn't skipped. Walks the scripts with the opcode-length table
        // so only REAL movie opcodes are touched (a raw byte scan would hit false
        // 0xF8/0xF9 bytes in the script offset tables). Length-preserving (0x5F).
        if (nopFieldScriptMovies(decompressed, fieldName, debugStream) > 0)
            totalMods++;
    }

    // Cave of the Gi interior (gidun_1..4 + the Seto chamber seto1): now reachable
    // in Free Roam via the cosin2 door re-open. These rooms play disc-keyed story
    // movies via "Set next movie" — on disc 3 (forced in Free Roam) they resolve to
    // placeholder entries (e.g. seto1's "No44") that crash on play, the same failure
    // mode as md1stin's intro. NOP the PMVIE/MOVIE opcodes (length-preserving 0x5F);
    // a no-op for any room without a movie.
    {
        const QString fl = fieldName.toLower();
        if (freeRoam && (fl == "seto1" || fl.startsWith("gidun"))) {
            if (nopFieldScriptMovies(decompressed, fieldName, debugStream) > 0)
                totalMods++;
        }
    }

    // spipe_2 (Underwater Reactor pipe): a "lock" Line entity freezes the player and
    // shows "Locked" when GameMoment >= 1299 (disc 2/3 gate). In Free Roam (disc 3)
    // that is always true, so the pipe is permanently locked. Flip the comparison to
    // '<= 1299' (IFSW GameMoment value 0x0513 oper 0x04 ">=" -> 0x05 "<="): the high
    // Free Roam moment now fails it, so the lock branch is skipped. Operand-only edit
    // via the FieldScriptEditor (jump preserved); handles multiple lock lines.
    if (freeRoam && fieldName.toLower() == "spipe_2") {
        QString fsErr;
        FieldScriptEditor ed;
        if (!ed.parse(decompressed, fsErr)) {
            debugStream << "  SPIPE2: FieldScriptEditor parse failed (" << fsErr << ")\n";
        } else {
            int n = 0, i = 0;
            const QByteArray anchor = QByteArray::fromHex("16200000130504"); // IFSW GameMoment >= 1299
            while ((i = ed.findOpcode(anchor, i)) >= 0) {
                QString e;
                if (ed.patchOperands(i, 6, QByteArray::fromHex("05"), e)) {
                    debugStream << "  SPIPE2 LOCK: GameMoment >=1299 -> <=1299 @instr " << i << "\n";
                    ++n;
                } else {
                    debugStream << "  SPIPE2 LOCK: patch failed @instr " << i << " (" << e << ")\n";
                }
                ++i;
            }
            if (n > 0) {
                QByteArray out = ed.assemble(fsErr);
                if (!out.isEmpty()) { decompressed = out; totalMods += n; debugStream << "  SPIPE2: " << n << " lock(s) unlocked, reassembled\n"; }
                else debugStream << "  SPIPE2: assemble failed (" << fsErr << ")\n";
            } else {
                debugStream << "  SPIPE2 LOCK: GameMoment>=1299 anchor not found\n";
            }
        }
    }

    // semkin_7 (submarine dock): the vs_ssol line forces the "take the submarine"
    // sequence — soldier battle #769, then a MAPJUMP to subin_2b (#408, sub interior).
    // In Free Roam we keep the fight but redirect the exit to the WORLD MAP at Junon
    // instead of boarding the sub. wm field 7 is Junon's world-map surface entry (it's
    // what Lower Junon, junonl1, MAPJUMPs to); we reuse its zero-coord form (coords are
    // ignored for wm dummy fields). Same 10-byte length, but routed via the
    // FieldScriptEditor for consistency. Anchor = the original MAPJUMP to subin_2b.
    if (freeRoam && fieldName.toLower() == "semkin_7") {
        QString fsErr;
        FieldScriptEditor ed;
        if (!ed.parse(decompressed, fsErr)) {
            debugStream << "  SEMKIN7: FieldScriptEditor parse failed (" << fsErr << ")\n";
        } else {
            int n = 0, i = 0;
            const QByteArray anchor = QByteArray::fromHex("6098017a0075ff2d00c0"); // MAPJUMP subin_2b (#408)
            const QByteArray wmJunon = QByteArray::fromHex("60070000000000000000"); // MAPJUMP wm7 (Junon surface)
            while ((i = ed.findOpcode(anchor, i)) >= 0) {
                QString e;
                if (ed.replaceAt(i, wmJunon, e)) {
                    debugStream << "  SEMKIN7: MAPJUMP subin_2b -> wm7 (Junon surface) @instr " << i << "\n";
                    ++n;            // same length; keep scanning from i
                } else {
                    debugStream << "  SEMKIN7: repoint failed @instr " << i << " (" << e << ")\n";
                    ++i;
                }
            }
            if (n > 0) {
                QByteArray out = ed.assemble(fsErr);
                if (!out.isEmpty()) { decompressed = out; totalMods += n; debugStream << "  SEMKIN7: " << n << " MAPJUMP(s) repointed to Junon surface, reassembled\n"; }
                else debugStream << "  SEMKIN7: assemble failed (" << fsErr << ")\n";
            } else {
                debugStream << "  SEMKIN7: subin_2b MAPJUMP anchor not found\n";
            }
        }
    }
    // gidun_1 (Cave of the Gi, first room): directr's Init script forces the party
    // to "Cloud | Red XIII | (Empty)" (PRTYE = ca 00 04 ff) on every entry. In Free
    // Roam we don't want the party overwritten. It's the Init script's FIRST
    // instruction (removeAt refuses a script start), so replace it in place with a
    // harmless WAIT 0 (24 00 00) — the player keeps their current party.
    if (freeRoam && fieldName.toLower() == "gidun_1") {
        QString fsErr;
        FieldScriptEditor ed;
        if (!ed.parse(decompressed, fsErr)) {
            debugStream << "  GIDUN1: FieldScriptEditor parse failed (" << fsErr << ")\n";
        } else {
            const int i = ed.findOpcode(QByteArray::fromHex("ca0004ff"), 0); // PRTYE Cloud|RedXIII|empty
            if (i < 0) {
                debugStream << "  GIDUN1: forced New-party (ca0004ff) not found\n";
            } else {
                QString e;
                if (ed.replaceAt(i, QByteArray::fromHex("240000"), e)) {     // -> WAIT 0
                    QByteArray out = ed.assemble(fsErr);
                    if (!out.isEmpty()) { decompressed = out; ++totalMods; debugStream << "  GIDUN1: removed forced New-party (PRTYE -> WAIT 0) @instr " << i << "\n"; }
                    else debugStream << "  GIDUN1: assemble failed (" << fsErr << ")\n";
                } else {
                    debugStream << "  GIDUN1: replaceAt failed (" << e << ")\n";
                }
            }
        }
    }

    // gidun_3 (Cave of the Gi, Gravity room): also writes GameMoment = 514
    // (SETWORD bank2[0] = 514, 81 20 00 02 02) — same disc-1 slam as seto1. NOP it
    // (0x5F x5, real bytecode at that offset, length-preserving).
    if (freeRoam && fieldName.toLower() == "gidun_3") {
        const int gm = decompressed.indexOf(QByteArray::fromHex("8120000202"));
        if (gm >= 0) {
            for (int j = 0; j < 5; ++j) decompressed[gm + j] = char(0x5f);
            ++totalMods;
            debugStream << "  GIDUN3: NOP'd GameMoment=514 write @" << gm << "\n";
        } else {
            debugStream << "  GIDUN3: GameMoment=514 anchor not found\n";
        }
    }

    // gidun_4 (Cave of the Gi, exit room): the LINEE exit-line trigger runs a
    // Bugenhagen cutscene that (a) SPLITs the party into a formation and (b) REQEWs
    // Red XIII's animation — both BLOCKING. In Free Roam the party is arbitrary: a
    // reduced party never fills the SPLIT slots, and Red XIII (an AP recruit) may be
    // absent, so his REQEW waits on a script that never runs -> softlock at the exit.
    // Fix: NOP the SPLIT (0x09 -> 0x5F no-ops, as for losinn) and de-block Red XIII's
    // REQEW (opcode 0x03 -> 0x01 REQ, fire-and-forget). Both length-preserving; the
    // dialog + Cloud/Bugenhagen animations still play.
    if (freeRoam && fieldName.toLower() == "gidun_4") {
        if (nopFieldScriptSplits(decompressed, fieldName, debugStream) > 0)
            totalMods++;
        const int at = decompressed.indexOf(QByteArray::fromHex("0307c3"));  // REQEW BALLET/Barret (entity 7)
        if (at >= 0) {
            decompressed[at] = static_cast<char>(0x01);                      // REQEW -> REQ
            ++totalMods;
            debugStream << "  GIDUN4: de-blocked Barret REQEW (0307c3 -> 0107c3) @" << at << "\n";
        } else {
            debugStream << "  GIDUN4: Barret REQEW anchor not found\n";
        }
        // The SAME pre-boss Bugenhagen cutscene ALSO REQEWs entity 13 = CID
        // (03 0d c4, immediately before the Barret one) — a party without Cid
        // hangs right after Bugenhagen's "near the end" line (reported softlock;
        // the earlier session misread entity 13 as Bugenhagen). REQEW -> REQ.
        const int cid = decompressed.indexOf(QByteArray::fromHex("030dc4"));
        if (cid >= 0) {
            decompressed[cid] = static_cast<char>(0x01);
            ++totalMods;
            debugStream << "  GIDUN4: de-blocked Cid REQEW (030dc4 -> 010dc4) @" << cid << "\n";
        } else {
            debugStream << "  GIDUN4: Cid REQEW anchor not found\n";
        }
        // Later scripts in the chain PRQEW entity 8 = TIFA three times (06 08 bc)
        // — same absent-member hang class. PRQEW -> PREQ (0x06 -> 0x04) keeps the
        // prioritized request without the blocking wait.
        int tp = 0, from = 0;
        while (true) {
            const int t = decompressed.indexOf(QByteArray::fromHex("0608bc"), from);
            if (t < 0) break;
            decompressed[t] = static_cast<char>(0x04);
            from = t + 1; ++tp;
        }
        if (tp) {
            ++totalMods;
            debugStream << "  GIDUN4: de-blocked " << tp << " Tifa PRQEW(s) (0608bc -> 0408bc)\n";
        }
        // STILL softlocking (2026-07-15): the same cutscene has THREE more blocking
        // ops the earlier passes missed — a JOIN (08 2d, the counterpart to the
        // NOP'd SPLIT: with a reduced Free Roam party it waits forever for members
        // to rejoin) and REQEWs on entity 3 = yougan (03 03 c3 / 03 03 c4). Full
        // decode of the cutscene @0x79b confirmed these are the only remaining
        // blockers. Patch via unique multi-op anchors (length-preserving):
        //   (a) 82 30 ad 02 | 08 2d | 03 03 c4  -> BITON ; JOIN->NOP ; REQEW->REQ
        //   (b) 71 01 | 03 03 c3 | 01 0d c3      -> REQEW yougan c3 -> REQ
        //   (c) 03 03 c4 | 01 0d c5              -> REQEW yougan c4 (mid) -> REQ
        struct GPatch { const char* from; const char* to; const char* what; };
        static const GPatch gp[] = {
            { "8230ad02082d0303c4", "8230ad025f5f0103c4", "JOIN NOP + yougan REQEW->REQ" },
            { "71010303c3010dc3",   "71010103c3010dc3",   "yougan REQEW c3 -> REQ" },
            { "0303c4010dc5",       "0103c4010dc5",       "yougan REQEW c4 -> REQ" },
        };
        for (const GPatch& g : gp) {
            const QByteArray a = QByteArray::fromHex(g.from), b = QByteArray::fromHex(g.to);
            const int at = decompressed.indexOf(a);
            if (at >= 0 && decompressed.indexOf(a, at + 1) < 0) {
                decompressed.replace(at, a.size(), b);
                ++totalMods;
                debugStream << "  GIDUN4: " << g.what << " @" << at << "\n";
            } else {
                debugStream << "  GIDUN4: anchor for '" << g.what << "' not found/ambiguous\n";
            }
        }
    }

    // cos_btm2 (Cosmo Canyon, arrival after the Cave of the Gi): the return cutscene
    // runs "Red XIII not available" (cd 00 04, removes him) then "Show menu Change
    // party" (49 00 07 00). In Free Roam that softlocks the party jump out of the
    // cave. NOP both (0x5F fillers, length-preserving); the WAIT between is kept.
    if (freeRoam && fieldName.toLower() == "cos_btm2") {
        const int at = decompressed.indexOf(QByteArray::fromHex("cd000424040049000700"));
        if (at >= 0) {
            decompressed[at] = decompressed[at + 1] = decompressed[at + 2] = char(0x5f);           // cd0004 -> 5f5f5f
            decompressed[at + 6] = decompressed[at + 7] = decompressed[at + 8] = decompressed[at + 9] = char(0x5f); // 49000700 -> 5f x4
            ++totalMods;
            debugStream << "  COSBTM2: NOP'd Red-XIII-not-available + change-party menu @" << at << "\n";
        } else {
            debugStream << "  COSBTM2: Red-XIII/change-party anchor not found\n";
        }
        // The same field ALSO has "Red XIII available" (cd 01 04) further on —
        // it re-grants him after the cave return scene. Red is an AP item; NOP.
        const int av = decompressed.indexOf(QByteArray::fromHex("cd0104"));
        if (av >= 0) {
            decompressed[av] = decompressed[av + 1] = decompressed[av + 2] = char(0x5f);
            ++totalMods;
            debugStream << "  COSBTM2: NOP'd Red XIII available (cd0104) @" << av << "\n";
        }
    }

    // cos_btm (Cosmo Canyon): the RED entity auto-joins Red XIII — "Red XIII
    // available" (cd 01 04) + "Unlock Red XIII in PHS menu" (cf 04). In Free Roam
    // Red XIII is an AP character item, so neuter the auto-join (NOP both, 0x5F).
    if (freeRoam && fieldName.toLower() == "cos_btm") {
        const int at = decompressed.indexOf(QByteArray::fromHex("cd0104cf04"));
        if (at >= 0) {
            decompressed[at] = decompressed[at + 1] = decompressed[at + 2] = char(0x5f);   // cd0104 -> 5f5f5f
            decompressed[at + 3] = decompressed[at + 4] = char(0x5f);                       // cf04   -> 5f5f
            ++totalMods;
            debugStream << "  COSBTM: NOP'd Red XIII auto-join (available + PHS unlock) @" << at << "\n";
        } else {
            debugStream << "  COSBTM: Red XIII auto-join anchor not found\n";
        }
        // Forced New-party Cloud+RedXIII (ca 00 04 ff) in the same scene chain —
        // overwrites the player's party in Free Roam. NOP (0x5F x4, mid-script safe).
        const int pe = decompressed.indexOf(QByteArray::fromHex("ca0004ff"));
        if (pe >= 0) {
            for (int j = 0; j < 4; ++j) decompressed[pe + j] = char(0x5f);
            ++totalMods;
            debugStream << "  COSBTM: NOP'd forced New-party Cloud+Red (ca0004ff) @" << pe << "\n";
        }
        // Also NOP the GameMoment = 523 write (SETWORD bank2[0] = 523, 81 20 00 0b 02):
        // like seto1's 514, it would slam the Free Roam moment back to a disc-1 value.
        const int gm = decompressed.indexOf(QByteArray::fromHex("8120000b02"));
        if (gm >= 0) {
            for (int j = 0; j < 5; ++j) decompressed[gm + j] = char(0x5f);
            ++totalMods;
            debugStream << "  COSBTM: NOP'd GameMoment=523 write @" << gm << "\n";
        } else {
            debugStream << "  COSBTM: GameMoment=523 anchor not found\n";
        }
    }

    // crcin_1 (Chocobo Square): Ester ('esto', the race manager) is hidden outside
    // the disc-1 racing window — her Init sets model VISIBILITY off (a4 00) by
    // default and only flips it on (a4 01) inside a `GameMoment == 1008` branch that
    // Free Roam (moment ~1997) never enters, so she never appears and chocobo racing
    // is unreachable. Force her visible: flip her default a4 00 -> a4 01,
    // length-preserving, anchored on the unique `SOLID-on; VISI-off; IFSW GM==1008`
    // sequence. (Phase 1 of Free Roam chocobo racing — talk-gate + race launch next.)
    if (freeRoam && fieldName.toLower() == "crcin_1") {
        // (1) Force Ester (esto) into a clean, TALKABLE state. A placed field model
        // is only talkable when it's BOTH visible AND solid (cf. the Kalm traveler
        // 'oman', which is just placed + SOLID with no VISI-off — no special "talk"
        // opcode exists). Ester's vanilla Init places her then runs a maze of
        // GameMoment(>=1008)/Var gates whose branches leave her either hidden or
        // SOLID-off in Free Roam. After her CHAR(8)+XYZI+DIR, overwrite the whole
        // 26-byte gate/branch block (7e01 c701 a4xx IFSW IFUB 7e00 c7xx a401) with an
        // unconditional anim + VISI-on + SOLID-on + no-ops, leaving the trailing RET.
        // Anchored on Ester-unique CHAR(8)+place+dir; length-preserving.
        const QByteArray esterAnc = QByteArray::fromHex("a108a500000cfe450000005000b30020");
        const int at = decompressed.indexOf(esterAnc);
        if (at >= 0) {
            const int b = at + 16;                                          // start of the state block
            decompressed[b + 0] = char(0x7e); decompressed[b + 1] = char(0x01);  // anim 1 (idle)
            decompressed[b + 2] = char(0xa4); decompressed[b + 3] = char(0x01);  // VISI on
            decompressed[b + 4] = char(0xc7); decompressed[b + 5] = char(0x01);  // SOLID on
            for (int j = 6; j < 26; ++j) decompressed[b + j] = char(0x5f);        // NOP the gates + branch
            ++totalMods;
            debugStream << "  CRCIN1: Ester -> unconditional visible+solid (talkable) @" << b << "\n";
        } else {
            debugStream << "  CRCIN1: Ester Init anchor not found\n";
        }
        // (2) Fully DISABLE kei1: it's placed at Ester's exact coords (alternate
        // NPCs for that spot), so it clips her AND blocks talking to her. Merely
        // hiding it (VISI off) left it solid + talkable. Overwrite its place+dir
        // (a5<11 bytes> + b3<3> = 14 bytes) with VISI-off + SOLID-off + no-ops
        // (a4 00 ; c7 00 ; 0x5f x10) so it's invisible, non-solid and un-talkable
        // (a hidden, un-placed NPC needs no position/facing). Anchored on the
        // kei1-unique CHAR+place+dir; length-preserving.
        const QByteArray keiAnc = QByteArray::fromHex("a107a500000cfe450000005000b30020");
        const int k = decompressed.indexOf(keiAnc);
        if (k >= 0) {
            decompressed[k + 2] = char(0xa4); decompressed[k + 3] = char(0x00);   // a4 00 (VISI off)
            decompressed[k + 4] = char(0xc7); decompressed[k + 5] = char(0x00);   // c7 00 (SOLID off)
            for (int j = 6; j < 16; ++j) decompressed[k + j] = char(0x5f);         // no-ops (was place+dir)
            ++totalMods;
            debugStream << "  CRCIN1: disabled kei1 (hide + unsolid) @" << k << "\n";
        } else {
            debugStream << "  CRCIN1: kei1 anchor not found\n";
        }
        // (3) Un-gate Ester's talk script: her Main bails at the start on
        // `IFUB Var[b0][0x8a] bit 0 OFF -> jump to end` (the racing-state flag Free
        // Roam never sets), so talking does nothing. Flip the comparison bitOFF(0x0a)
        // -> bitON(0x09): "bit 0 ON -> skip" — since the flag stays OFF in Free Roam,
        // the branch is never taken and her race dialog runs.
        const int e = decompressed.indexOf(QByteArray::fromHex("14b08a000a45")); // IFUB Var[b0][0x8a].0 OFF -> jump
        if (e >= 0) {
            decompressed[e + 4] = char(0x09);   // oper 0x0a (bitOFF) -> 0x09 (bitON)
            ++totalMods;
            debugStream << "  CRCIN1: un-gated Ester talk (bitOFF -> bitON) @" << (e + 4) << "\n";
        } else {
            debugStream << "  CRCIN1: Ester talk-gate anchor not found\n";
        }
    }

    // seto1 (Cave of the Gi, Seto chamber): the end-scene scripts write
    // $GameMoment = 514 (SETWORD bank2[0] = 0x0202) — a disc-1 story value — right
    // before granting the Seraph Comb and MAPJUMPing to Cosmo Canyon. In Free Roam
    // (moment ~1997) that write slams the moment down to 514 and re-locks everything
    // gated on a high moment (crater barrier, etc.). Delete every such write via the
    // re-offsetting FieldScriptEditor (the item grant + exit are untouched). There
    // are two in seto1; both are real script instructions (the value 514 does not
    // appear anywhere in the AKAO/data tail).
    if (freeRoam && fieldName.toLower() == "seto1") {
        // Seto's chamber forces the party to Cloud+RedXIII (ca 00 04 ff) — the
        // beat that re-adds Red even with gidun_1's PRTYE patched. Red is an AP
        // character item; NOP the 4 bytes (unique, in-script @0x4e9).
        const int pe = decompressed.indexOf(QByteArray::fromHex("ca0004ff"));
        if (pe >= 0) {
            for (int j = 0; j < 4; ++j) decompressed[pe + j] = char(0x5f);
            ++totalMods;
            debugStream << "  SETO1: NOP'd forced New-party Cloud+Red (ca0004ff) @" << pe << "\n";
        } else {
            debugStream << "  SETO1: forced New-party anchor not found\n";
        }
        QString fsErr;
        FieldScriptEditor ed;
        if (!ed.parse(decompressed, fsErr)) {
            debugStream << "  SETO1: FieldScriptEditor parse failed (" << fsErr << ")\n";
        } else {
            int n = 0, i = 0;
            const QByteArray anchor = QByteArray::fromHex("8120000202"); // SETWORD $GameMoment = 514
            while ((i = ed.findOpcode(anchor, i)) >= 0) {
                QString e;
                if (ed.removeAt(i, e)) {
                    debugStream << "  SETO1: removed $GameMoment=514 write @instr " << i << "\n";
                    ++n;            // instruction deleted; the next one shifts to i
                } else {
                    debugStream << "  SETO1: removeAt failed @instr " << i << " (" << e << ") — skipping\n";
                    ++i;
                }
            }
            if (n > 0) {
                QByteArray out = ed.assemble(fsErr);
                if (!out.isEmpty()) { decompressed = out; totalMods += n; debugStream << "  SETO1: " << n << " GameMoment=514 write(s) removed, reassembled\n"; }
                else debugStream << "  SETO1: assemble failed (" << fsErr << ")\n";
            } else {
                debugStream << "  SETO1: $GameMoment=514 anchor not found\n";
            }
        }
    }

    // sininb1 (Shinra Mansion basement, Vincent's coffin room): the lin0 Line's
    // recruitment script makes Vincent join. In Free Roam Vincent is an AP item, so
    // neuter the join — remove "Vincent available" (cd 01 07, char id 7) and the
    // follow-up "Change party" menu (49 00 07 00). The quest-complete flag (line 15,
    // BITON Var[13][80].2) is LEFT intact: it is the AP check's detection bit, and
    // the "join party field" animation (line 16) is kept. Re-offsetting removeAt.
    if (freeRoam && fieldName.toLower() == "sininb1") {
        QString fsErr;
        FieldScriptEditor ed;
        if (!ed.parse(decompressed, fsErr)) {
            debugStream << "  SININB1: FieldScriptEditor parse failed (" << fsErr << ")\n";
        } else {
            int n = 0;
            const char* const anchors[2] = { "cd0107", "49000700" };  // Vincent available; Change-party menu
            for (const char* hx : anchors) {
                const QByteArray anc = QByteArray::fromHex(hx);
                const int i = ed.findOpcode(anc, 0);
                if (i < 0) { debugStream << "  SININB1: anchor " << hx << " not found\n"; continue; }
                QString e;
                if (ed.removeAt(i, e)) { debugStream << "  SININB1: removed " << hx << " @instr " << i << "\n"; ++n; }
                else debugStream << "  SININB1: removeAt failed for " << hx << " (" << e << ")\n";
            }
            if (n > 0) {
                QByteArray out = ed.assemble(fsErr);
                if (!out.isEmpty()) { decompressed = out; totalMods += n; debugStream << "  SININB1: " << n << " Vincent-join opcode(s) removed, reassembled\n"; }
                else debugStream << "  SININB1: assemble failed (" << fsErr << ")\n";
            }
        }
    }

    // yufy1 (Wutai, Yuffie's house): the YUFI entity's join script makes Yuffie
    // available — this is the recruit path reachable in Free Roam (the forest
    // encounter can't trigger at the Free Roam game moment). Yuffie is an AP
    // item, so
    // neuter the join — remove "Yuffie available" (cd 01 05, char id 5). Both
    // BITONs are LEFT intact: Var[3][189].4 (82 30 bd 04, set right before the
    // join = the AP check's detection bit, savemap 0xCA4+189 bit 4) and
    // Var[3][207].6. Unlike Vincent's sininb1 there is no change-party MENU to
    // remove (verified against the vanilla script bytes). Re-offsetting removeAt.
    if (freeRoam && fieldName.toLower() == "yufy1") {
        QString fsErr;
        FieldScriptEditor ed;
        if (!ed.parse(decompressed, fsErr)) {
            debugStream << "  YUFY1: FieldScriptEditor parse failed (" << fsErr << ")\n";
        } else {
            const QByteArray anc = QByteArray::fromHex("cd0105");  // Yuffie available
            const int i = ed.findOpcode(anc, 0);
            if (i < 0) {
                debugStream << "  YUFY1: anchor cd0105 not found\n";
            } else {
                QString e;
                if (ed.removeAt(i, e)) {
                    QByteArray out = ed.assemble(fsErr);
                    if (!out.isEmpty()) {
                        decompressed = out; ++totalMods;
                        debugStream << "  YUFY1: Yuffie-join CHRAVAIL removed, reassembled\n";
                    } else {
                        debugStream << "  YUFY1: assemble failed (" << fsErr << ")\n";
                    }
                } else {
                    debugStream << "  YUFY1: removeAt failed (" << e << ")\n";
                }
            }
        }
    }

    // yougan2 (forest encounter, post-fight Yuffie recruitment dialogue): winning
    // the world-map "Mystery Ninja" battle drops the player here, where the
    // dialogue ends in "Yuffie available" + a "name Yuffie" menu. Yuffie is an AP
    // item in Free Roam, so neuter the recruit — remove CHRAVAIL "Yuffie
    // available" (cd 01 05, char id 5) and the name-entry MENU (49 00 06 05 =
    // MENU type 6 name-entry, char 5). The encounter-state BITONs (Var[3][207].0
    // ON / Var[3][133].0 OFF) right after CHRAVAIL are KEPT — they track the
    // forest encounter as done so it self-limits. (The fight itself is a
    // world-map encounter, not in this field, so it can't be removed here.)
    if (freeRoam && fieldName.toLower() == "yougan2") {
        QString fsErr;
        FieldScriptEditor ed;
        if (!ed.parse(decompressed, fsErr)) {
            debugStream << "  YOUGAN2: FieldScriptEditor parse failed (" << fsErr << ")\n";
        } else {
            int n = 0;
            const char* const anchors[2] = { "cd0105", "49000605" };  // Yuffie available; name-entry menu
            for (const char* hx : anchors) {
                const QByteArray anc = QByteArray::fromHex(hx);
                const int i = ed.findOpcode(anc, 0);
                if (i < 0) { debugStream << "  YOUGAN2: anchor " << hx << " not found\n"; continue; }
                if (ed.findOpcode(anc, i + 1) >= 0) { debugStream << "  YOUGAN2: anchor " << hx << " ambiguous, skipped\n"; continue; }
                QString e;
                if (ed.removeAt(i, e)) { debugStream << "  YOUGAN2: removed " << hx << " @instr " << i << "\n"; ++n; }
                else debugStream << "  YOUGAN2: removeAt failed for " << hx << " (" << e << ")\n";
            }
            if (n > 0) {
                QByteArray out = ed.assemble(fsErr);
                if (!out.isEmpty()) { decompressed = out; totalMods += n; debugStream << "  YOUGAN2: " << n << " Yuffie-recruit opcode(s) removed, reassembled\n"; }
                else debugStream << "  YOUGAN2: assemble failed (" << fsErr << ")\n";
            }
        }
    }

    // bugin1b (Bugenhagen's observatory, upper room): the vanilla 'directr' Main
    // script fades out and MAPJUMPs to fship_4 (#74, the Highwind) — a story
    // transition that in Free Roam warps the player onto the airship. Redirect it
    // to bugin1a (#541, the observatory entrance) so the room stays self-contained.
    // Same 10-byte MAPJUMP; only the destination field id changes (coords/dir stay
    // 0, as in vanilla). Anchor = the MAPJUMP to fship_4. NOT Gold-Saucer-authored;
    // this is a vanilla jump we neutralize for Free Roam.
    if (freeRoam && fieldName.toLower() == "bugin1b") {
        QString fsErr;
        FieldScriptEditor ed;
        if (!ed.parse(decompressed, fsErr)) {
            debugStream << "  BUGIN1B: FieldScriptEditor parse failed (" << fsErr << ")\n";
        } else {
            int n = 0, i = 0;
            const QByteArray anchor  = QByteArray::fromHex("604a0000000000000000"); // MAPJUMP fship_4 (#74)
            const QByteArray toBugin = QByteArray::fromHex("601d0200000000000000"); // MAPJUMP bugin1a (#541)
            while ((i = ed.findOpcode(anchor, i)) >= 0) {
                QString e;
                if (ed.replaceAt(i, toBugin, e)) {
                    debugStream << "  BUGIN1B: MAPJUMP fship_4 -> bugin1a @instr " << i << "\n";
                    ++n;            // same length; keep scanning from i
                } else {
                    debugStream << "  BUGIN1B: repoint failed @instr " << i << " (" << e << ")\n";
                    ++i;
                }
            }
            if (n > 0) {
                QByteArray out = ed.assemble(fsErr);
                if (!out.isEmpty()) { decompressed = out; totalMods += n; debugStream << "  BUGIN1B: " << n << " MAPJUMP(s) repointed to bugin1a, reassembled\n"; }
                else debugStream << "  BUGIN1B: assemble failed (" << fsErr << ")\n";
            } else {
                debugStream << "  BUGIN1B: fship_4 MAPJUMP anchor not found\n";
            }
        }
    }
    if (freeRoam && fieldName.toLower() == "cosin2") {
        // Free Roam: re-open the Cave of the Gi door. The door is run by the 'directr'
        // (entity 3) S0-Main director, gated on the disc-1 GameMoment [502,514) window;
        // Free Roam (moment 1997) is outside it, so the open never fires. We drive this
        // through the re-offsetting FieldScriptEditor so the walkmesh triangle-activate
        // can be INSERTED at exactly the spot the hand-verified Makou fix used (after the
        // door-open REQs, BOTH screen-fades intact) instead of overwriting a fade. All
        // edits land on one parse and are re-emitted with offsets + jumps recomputed.
        QString fsErr;
        FieldScriptEditor ed;
        if (!ed.parse(decompressed, fsErr)) {
            debugStream << "  GI_CAVE: FieldScriptEditor parse failed (" << fsErr << ") — left vanilla\n";
        } else {
            int edits = 0;
            auto hx = [](const char* s){ return QByteArray::fromHex(s); };
            // overwrite operand bytes of an opcode located by its full-opcode prefix
            // (works on jump opcodes; the symbolic jump target is preserved)
            auto patchOp = [&](const char* findHex, int off, const char* bytesHex, const char* tag){
                int i = ed.findOpcode(hx(findHex));
                if (i < 0) { debugStream << "  GI_CAVE " << tag << ": anchor not found\n"; return; }
                QString e;
                if (ed.patchOperands(i, off, hx(bytesHex), e)) { debugStream << "  GI_CAVE " << tag << ": ok @instr " << i << "\n"; ++edits; }
                else debugStream << "  GI_CAVE " << tag << ": patch failed (" << e << ")\n";
            };
            // replace an opcode reached by navigating `delta` ops from a unique span
            // anchor; verify the target opcode id before replacing.
            auto replaceNav = [&](const char* anchorHex, int delta, quint8 expectId, const char* newHex, const char* tag){
                int a = ed.findBytes(hx(anchorHex));
                if (a < 0) { debugStream << "  GI_CAVE " << tag << ": anchor not found\n"; return; }
                int i = a + delta;
                QByteArray b = ed.instrBytes(i);
                if (b.isEmpty() || quint8(b.at(0)) != expectId) { debugStream << "  GI_CAVE " << tag << ": nav id mismatch @instr " << i << "\n"; return; }
                QString e;
                if (ed.replaceAt(i, hx(newHex), e)) { debugStream << "  GI_CAVE " << tag << ": ok @instr " << i << "\n"; ++edits; }
                else debugStream << "  GI_CAVE " << tag << ": replace failed (" << e << ")\n";
            };

            // --- door models open + non-solid -----------------------------------
            // model-09 controller (placed 9d ff / 3e 01): a5(place), b3(dir), 7e(anim),
            // c7(SOLID). Force open + non-solid.
            replaceNav("a500009dff3e01",       2, 0x7e, "7e00", "DOOR ctrl 7e");
            replaceNav("a500009dff3e01",       3, 0xc7, "c700", "DOOR ctrl c7");
            // D1 / D2 leaves: OFST then SOLID -> non-solid
            replaceNav("c300000096fff2005200", 1, 0xc7, "c700", "DOOR D1a");
            replaceNav("c3000000a6fff200f0ff", 1, 0xc7, "c700", "DOOR D2a");
            // live collision controller close branch (IFUB &4 ; 7e 01 ; c7 01) -> open
            replaceNav("1430aa0406097e01",     1, 0x7e, "7e00", "DOORCTRL 7e");
            replaceNav("1430aa0406097e01",     2, 0xc7, "c700", "DOORCTRL c7");

            // --- BUGEN story cutscene -> RET (no story, no solo-party SPLIT) -----
            {
                int i = ed.findBytes(hx("33014a010304c8ab040a02"));
                if (i < 0) debugStream << "  GI_CAVE STORY: anchor not found\n";
                else { QString e; if (ed.replaceAt(i, hx("00"), e)) { debugStream << "  GI_CAVE STORY: cutscene -> RET @instr " << i << "\n"; ++edits; } else debugStream << "  GI_CAVE STORY: failed (" << e << ")\n"; }
            }

            // --- directr window/story-bit gates (operand edits; jumps preserved) -
            // @1892 IFSW 'GameMoment < 514' -> 'GameMoment > 1' (value 514->1, oper 03->02)
            patchOp("162000000202032b", 4, "010002", "WINDOW <514->>1");
            // @1900 IFUB '& 4' -> '| 4' (oper 06->08) so the open ignores the story bit
            patchOp("1430aa040625",     4, "08",     "WINDOW &->|");
            // directr REQs D1/D2 script 4 (closed leaves); repoint to script 3 (slide open)
            patchOp("0111c4",           2, "c3",     "OPENCALL D1");
            patchOp("0112c4",           2, "c3",     "OPENCALL D2");

            // --- INSERT walkmesh activate (the byte-patch could not do this) -----
            // IDLCK 6d 29 00 00 = "Activate triangle #41", placed right after directr's
            // second FADE (immediately before FADEW), exactly where the hand-verified
            // Makou edit inserted it. The editor recomputes the directr IFSW window jumps
            // (>=502 / >1) that now span these 4 inserted bytes.
            {
                int f = ed.findOpcode(hx("6b00000000000801ff")); // directr second FADE
                if (f < 0) debugStream << "  GI_CAVE TRIANGLE: FADE anchor not found\n";
                else { QString e; if (ed.insertBefore(f + 1, hx("6d290000"), e)) { debugStream << "  GI_CAVE TRIANGLE: IDLCK tri#41 inserted @instr " << (f+1) << "\n"; ++edits; } else debugStream << "  GI_CAVE TRIANGLE: insert failed (" << e << ")\n"; }
            }

            if (edits > 0) {
                const int wasSize = decompressed.size();
                QByteArray out = ed.assemble(fsErr);
                if (out.isEmpty()) {
                    debugStream << "  GI_CAVE: assemble failed (" << fsErr << ") — left vanilla\n";
                } else {
                    decompressed = out;
                    totalMods += edits;
                    debugStream << "  GI_CAVE: " << edits << " editor edit(s) applied, reassembled ("
                                << out.size() << " bytes, was " << wasSize << ")\n";
                }
            } else {
                debugStream << "  GI_CAVE: no edits applied (already patched or anchors missing)\n";
            }
        }
    }

    // --- Free Roam: suppress the Kalm Traveler gold-chocobo grant ------------
    // In elmin4_2 the 'choko' entity (script 1) awards a Gold Chocobo into the
    // stable when the Desert Rose trade sets its trigger bit (savemap addr 0x57
    // bit 4). In Free Roam the Gold Chocobo must come ONLY from Archipelago
    // (it gates ocean traversal), so we neutralise the in-game grant while
    // leaving the trade itself (and thus the AP "Show Gold Chocobo" check, which
    // fires on bit 4 being set) intact.
    //
    // Length-preserving patch: at the start of the grant body insert a JMPFL
    // straight to the existing `BITOFF 57 04` cleanup, NOP-filling everything in
    // between (the stable menu, slot search, chocobo data writes, occupancy
    // BITONs and the count increment). The trigger bit is still cleared at the
    // end exactly as vanilla, so the chocobo NPC disappears cleanly and nothing
    // is added to the stable.
    if (freeRoam && fieldName.toLower() == "elmin4_2") {
        // 'choko' script 1 @ grant start: IFUBL 33 58 59 02 D9 02
        static const unsigned char kGrantStart[] = {0x15,0x33,0x58,0x59,0x02,0xD9,0x02};
        // cleanup: BITOFF (bank 0xF0) addr 0x57 bit 4  — clears the trigger
        static const unsigned char kGrantTail[]  = {0x83,0xF0,0x57,0x04};
        auto findUnique = [&](const unsigned char* pat, int n) -> int {
            int first = -1;
            for (int i = 0; i + n <= decompressed.size(); ++i) {
                bool m = true;
                for (int k = 0; k < n; ++k)
                    if (static_cast<quint8>(decompressed[i + k]) != pat[k]) { m = false; break; }
                if (m) {
                    if (first >= 0) return -2;   // not unique
                    first = i;
                }
            }
            return first;
        };
        int s = findUnique(kGrantStart, sizeof(kGrantStart));
        int t = findUnique(kGrantTail,  sizeof(kGrantTail));
        if (s < 0 || t < 0) {
            debugStream << "  GOLD_CHOCO: anchors not found/unique (s=" << s
                        << " t=" << t << ") — skipping\n";
        } else if (static_cast<quint8>(decompressed[s]) == 0x11) {
            debugStream << "  GOLD_CHOCO: already patched — skipping\n";
        } else if (t <= s) {
            debugStream << "  GOLD_CHOCO: tail before start (t=" << t << " s=" << s
                        << ") — skipping\n";
        } else {
            int off = t - (s + 1);               // JMPFL target = (operand addr) + offset
            if (off > 0xFFFF) {
                debugStream << "  GOLD_CHOCO: jump distance " << off
                            << " too large — skipping\n";
            } else {
                decompressed[s]     = static_cast<char>(0x11);          // JMPFL
                decompressed[s + 1] = static_cast<char>(off & 0xFF);    // offset lo
                decompressed[s + 2] = static_cast<char>((off >> 8) & 0xFF); // offset hi
                for (int k = s + 3; k < t; ++k)
                    decompressed[k] = static_cast<char>(0x5F);          // NOP fill
                debugStream << "  GOLD_CHOCO: suppressed gold-chocobo grant in elmin4_2 "
                            << "(JMPFL @" << s << " +" << off << " -> BITOFF @" << t << ")\n";
                totalMods++;
            }
        }
    }

    // --- Free Roam: suppress the Kalm Traveler materia grants ----------------
    // elmin4_2 also hands out materia on trade-in: Guide Book -> Underwater
    // (0x11), Earth Harp -> Master Command (0x30) + Master Summon (0x5A) +
    // Master Magic (0x49). Those trades are AP checks (the trade trigger bits
    // bank13/0x57 bit0-3 fire the "Show Guide Book / Earth Harp 1-3" locations),
    // so the in-game materia is a double-dip. The generic SMTRA->BITON AP pass
    // can't convert these (its lookup is keyed by materia name, not the trade
    // text), so it leaves the vanilla grant. NOP each of the 4 direct-value
    // SMTRA grants (7 bytes -> 0x5F); the surrounding trade BITON/BITOFF + dialog
    // are untouched, so the AP checks still fire. Each grant is uniquely preceded
    // by the lead-in C5 00 50 00 33 01 4A 01 then the SMTRA 5B 00 00 <mat>.
    if (freeRoam && fieldName.toLower() == "elmin4_2") {
        static const unsigned char kLeadIn[] = {0xC5,0x00,0x50,0x00,0x33,0x01,0x4A,0x01};
        static const QSet<quint8> kKalmMateria = {0x11, 0x30, 0x49, 0x5A};
        const int L = static_cast<int>(sizeof(kLeadIn));
        int nopped = 0;
        for (int i = 0; i + L + 7 <= decompressed.size(); ++i) {
            bool lead = true;
            for (int k = 0; k < L; ++k)
                if (static_cast<quint8>(decompressed[i + k]) != kLeadIn[k]) { lead = false; break; }
            if (!lead) continue;
            const int s = i + L;                                   // SMTRA opcode offset
            if (static_cast<quint8>(decompressed[s])     != 0x5B) continue;  // SMTRA
            if (static_cast<quint8>(decompressed[s + 1]) != 0x00) continue;  // bank0 (direct)
            if (static_cast<quint8>(decompressed[s + 2]) != 0x00) continue;  // bank1 (direct)
            const quint8 mat = static_cast<quint8>(decompressed[s + 3]);
            if (!kKalmMateria.contains(mat)) continue;
            for (int k = 0; k < 7; ++k) decompressed[s + k] = static_cast<char>(0x5F); // NOP
            nopped++;
            debugStream << "  KALM_MATERIA: NOP SMTRA @" << s << " matId 0x"
                        << QString::number(mat, 16) << " (trade materia suppressed)\n";
        }
        if (nopped) {
            totalMods++;
            debugStream << "  KALM_MATERIA: suppressed " << nopped
                        << " Kalm Traveler materia grant(s) in elmin4_2\n";
        } else {
            debugStream << "  KALM_MATERIA: no grants found (already patched / not present)\n";
        }
    }

    // (Diamond Weapon is fully hidden in Free Roam — his ambient spawn is
    // neutralized in wm0.ev, so fr_e is never entered and needs no patch.)

    // --- Free Roam: skip the Fort Condor (convil_2) post-minigame movie --------
    // After the Condor minigame, the "event" cutscene runs PMVIE(set movie #33) ;
    // WAIT 1 ; MOVIE(play). On disc 3 (forced in Free Roam) movie #33 is a "No33"
    // placeholder that doesn't exist, so MOVIE crashes the game. NOP the set-movie
    // (F8 21 -> 5F 5F) and the play-movie (F9 -> 5F); the surrounding music/dialog
    // is untouched. (Movie opcodes are 0xF8 PMVIE / 0xF9 MOVIE — verified by
    // disassembly; the in-tree getOpcodeName labels for 0xD8/0xD9 are inaccurate.)
    if (freeRoam && fieldName.toLower() == "convil_2") {
        static const QByteArray kCondorMovie = QByteArray::fromHex("f8212401 00f9");
        int at = decompressed.indexOf(kCondorMovie);
        if (at < 0) {
            // already patched? (set-movie NOP'd)
            if (decompressed.indexOf(QByteArray::fromHex("5f5f2401005f")) >= 0)
                debugStream << "  CONDOR_MOVIE: already patched — skipping\n";
            else
                debugStream << "  CONDOR_MOVIE: PMVIE/MOVIE anchor not found — skipping\n";
        } else {
            decompressed[at]     = static_cast<char>(0x5F); // PMVIE opcode -> NOP
            decompressed[at + 1] = static_cast<char>(0x5F); // PMVIE operand (movie 33) -> NOP
            decompressed[at + 5] = static_cast<char>(0x5F); // MOVIE -> NOP
            debugStream << "  CONDOR_MOVIE: NOP'd Fort Condor post-minigame movie @0x"
                        << QString::number(at, 16) << "\n";
            totalMods++;
        }
    }

    // --- Free Roam: skip the Icicle Inn (snow) Shinra-blockade cutscene --------
    // man1's contact script asks "It's dangerous, please don't go!" and, if the
    // answer (Var[5][16]) == 1, runs the Elena/Shinra confrontation that seals the
    // town exits -> Free Roam softlock. The gate is an IFUBL "Var[5][16] == 1, else
    // goto label 3 (skip)". Change the compared value 1 -> 0xFF so the test can never
    // be true (the answer is only ever 1/2, bank-5 temp default 0), making it always
    // take the skip branch. Unique anchor; length-preserving 1-byte edit; idempotent.
    if (freeRoam && fieldName.toLower() == "snow") {
        static const QByteArray kSnowGate = QByteArray::fromHex("15501001000901"); // IFUBL Var5[16]==1, jmp 0x0109
        int at = decompressed.indexOf(kSnowGate);
        if (at < 0) {
            if (decompressed.indexOf(QByteArray::fromHex("155010ff000901")) >= 0)
                debugStream << "  SNOW_SHINRA: already patched — skipping\n";
            else
                debugStream << "  SNOW_SHINRA: IFUBL Var[5][16]==1 gate not found "
                               "(version differs?) — skipping\n";
        } else {
            decompressed[at + 3] = static_cast<char>(0xFF);   // == 1 -> == 0xFF (never) => always skip
            debugStream << "  SNOW_SHINRA: neutralized Elena/Shinra blockade gate @0x"
                        << QString::number(at, 16) << "\n";
            totalMods++;
        }
    }

    // --- Free Roam: force Kalm to its disc-1 behaviour (music + inn rest) ------
    // Every Kalm field (elm*) gates music AND inn-rest behaviour on
    //   IFSW Var[2][0] (game_moment) > 999   [bytes: 16 20 00 00 e7 03 02 <jmp>]
    // choosing disc-1 (the jump/"else" branch — FF7 IF jumps when the test is
    // FALSE) vs the post-Meteor path (fall-through, taken when game_moment > 999).
    // Free Roam forces game_moment=1997, so the whole town runs its disc-2/3 path:
    // the post-Meteor theme (the "disc-3 music in Kalm" report) AND the disc-2/3
    // inn-rest branch, which deadlocks (the Kalm inn freeze). Flip the compared
    // value 999 (0x03E7) -> 65535 (0xFFFF): game_moment is a u16 so it can never
    // exceed 0xFFFF, the test is always FALSE, and every gate takes the disc-1
    // branch (Anxious Heart + the normal inn rest). Length-preserving (2 bytes),
    // idempotent (ff ff no longer matches e7 03). Bounded to the script bytecode
    // region [sec0+4, +posTexts) so dialog / other-section bytes can't false-match.
    if (freeRoam && fieldName.toLower().startsWith("elm")
        && decompressed.size() >= 6 + 9 * 4) {
        quint32 sec0b = 0;
        memcpy(&sec0b, decompressed.constData() + 6, 4);
        int sd = static_cast<int>(sec0b) + 4;
        if (sd + 6 <= decompressed.size()) {
            quint16 posTexts = 0;
            memcpy(&posTexts, decompressed.constData() + sd + 4, 2);
            int hi = sd + static_cast<int>(posTexts);
            if (hi > decompressed.size() || hi <= sd) hi = decompressed.size();
            static const QByteArray kGate = QByteArray::fromHex("16200000e70302"); // IFSW game_moment>999
            int patched = 0;
            int at = sd;
            while ((at = decompressed.indexOf(kGate, at)) >= 0 && at < hi) {
                decompressed[at + 4] = static_cast<char>(0xFF);  // value 0x03E7 -> 0xFFFF
                decompressed[at + 5] = static_cast<char>(0xFF);  // (game_moment > 65535 = never)
                at += kGate.size();
                patched++;
            }
            if (patched) {
                totalMods++;
                debugStream << "  KALM_DISC1: " << fieldName << " forced disc-1 ("
                            << patched << " game_moment>999 gate(s) neutralized)\n";
            }
        }
    }

    // --- Free Roam: re-gate the Midgar Sector-5 entry walkmesh on the
    // Key-to-Sector-5 POSSESSION bit -----------------------------------------
    // mds5_5 gates its entry triangle on  IFUB Var[15][38] bitOFF 3
    //   [bytes: 14 f0 26 03 0a <jmp>]  == savemap 0x0FCA.3.
    // That flag is ALSO the Bone Village "Key To Sector 5" pickup's detection
    // bit, so the client setting it on receipt (to open the walkmesh) collided
    // with the AP check. Repoint the test to the key-item POSSESSION bit
    // Var[1][0x43].5  [14 10 43 05 0a] (set by AP delivery): the walkmesh opens
    // from HOLDING the key, freeing 0x0FCA.3 for the check. Length-preserving
    // (operand bytes only), idempotent (after patch the old needle won't match),
    // bounded to the script bytecode region. 3 copies in mds5_5.
    if (freeRoam && fieldName.toLower() == "mds5_5"
        && decompressed.size() >= 6 + 9 * 4) {
        quint32 sec0b = 0;
        memcpy(&sec0b, decompressed.constData() + 6, 4);
        int sd = static_cast<int>(sec0b) + 4;
        if (sd + 6 <= decompressed.size()) {
            quint16 posTexts = 0;
            memcpy(&posTexts, decompressed.constData() + sd + 4, 2);
            int hi = sd + static_cast<int>(posTexts);
            if (hi > decompressed.size() || hi <= sd) hi = decompressed.size();
            static const QByteArray kGate = QByteArray::fromHex("14f026030a"); // IFUB Var[15][38] bitOFF 3
            int patched = 0;
            int at = sd;
            while ((at = decompressed.indexOf(kGate, at)) >= 0 && at < hi) {
                decompressed[at + 1] = static_cast<char>(0x10); // bank 15 -> bank 1
                decompressed[at + 2] = static_cast<char>(0x43); // addr 0x26 -> 0x43 (key-item byte)
                decompressed[at + 3] = static_cast<char>(0x05); // bit 3 -> 5 (Key to Sector 5)
                at += kGate.size();
                patched++;
            }
            if (patched) {
                totalMods++;
                debugStream << "  SECTOR5_GATE: mds5_5 re-gated on Key-to-Sector-5 possession ("
                            << patched << " IFUB test(s) repointed)\n";
            }
        }
    }

    // --- Free Roam: re-gate the Shinra Mansion basement on the Basement-Key
    // POSSESSION bit ---------------------------------------------------------
    // sininb2 gates basement access on  IFUB Var[1][232] bitOFF 1
    //   [bytes: 14 10 e8 01 0a <jmp>]  == savemap 0x0C8C.1, which is the
    // "Key To Basement" (sinin2_1) pickup's detection bit. Repoint to the
    // key-item POSSESSION bit Var[1][0x43].4 [14 10 43 04 0a] so the basement
    // opens from holding the key and the AP check (re-introduced) stays
    // obtainable. Length-preserving, idempotent. 1 copy in sininb2.
    if (freeRoam && fieldName.toLower() == "sininb2"
        && decompressed.size() >= 6 + 9 * 4) {
        quint32 sec0b = 0;
        memcpy(&sec0b, decompressed.constData() + 6, 4);
        int sd = static_cast<int>(sec0b) + 4;
        if (sd + 6 <= decompressed.size()) {
            quint16 posTexts = 0;
            memcpy(&posTexts, decompressed.constData() + sd + 4, 2);
            int hi = sd + static_cast<int>(posTexts);
            if (hi > decompressed.size() || hi <= sd) hi = decompressed.size();
            static const QByteArray kGate = QByteArray::fromHex("1410e8010a"); // IFUB Var[1][232] bitOFF 1
            int patched = 0;
            int at = sd;
            while ((at = decompressed.indexOf(kGate, at)) >= 0 && at < hi) {
                decompressed[at + 2] = static_cast<char>(0x43); // addr 0xE8 -> 0x43 (key-item byte)
                decompressed[at + 3] = static_cast<char>(0x04); // bit 1 -> 4 (Basement Key)
                at += kGate.size();
                patched++;
            }
            if (patched) {
                totalMods++;
                debugStream << "  BASEMENT_GATE: sininb2 re-gated on Basement-Key possession ("
                            << patched << " IFUB test(s) repointed)\n";
            }
        }
    }

    // --- Free Roam: re-gate the Icicle slope (snow) snowboard / glacier-map
    // ACCESS checks on the key-item INVENTORY bits ----------------------------
    // snowboard/glacier "obtained" is tracked by the story flags Var[1][130]
    // bit1 (snowboard) / bit6 (glacier). Those flags are set by the in-game
    // pickup AND are the AP location's detection bit, so the client can't set
    // them on AP receipt without hiding the location. The menu key-item bits
    // (Var[1][0x46].2 / Var[1][0x45].4) ARE set on AP receipt (KEY_ITEM_FLAGS),
    // so we repoint the "do you HAVE it" (bitON, oper 0x09) access checks in the
    // snow-slope field to read those instead:
    //   IFUB Var[1][130] bitON 1  [14 10 82 01 09] -> Var[1][0x46] bit2 [14 10 46 02 09]
    //   IFUB Var[1][130] bitON 6  [14 10 82 06 09] -> Var[1][0x45] bit4 [14 10 45 04 09]
    // The bitOFF giver/visibility checks (snmin1/snmin2) and the detection BITON
    // are left ON the story flag, so the in-game location still works and the
    // in-game pickup no longer grants ride access. Length-preserving, bounded to
    // the script bytecode region, idempotent.
    if (freeRoam && fieldName.toLower() == "snow"
        && decompressed.size() >= 6 + 9 * 4) {
        quint32 sec0b = 0;
        memcpy(&sec0b, decompressed.constData() + 6, 4);
        int sd = static_cast<int>(sec0b) + 4;
        if (sd + 6 <= decompressed.size()) {
            quint16 posTexts = 0;
            memcpy(&posTexts, decompressed.constData() + sd + 4, 2);
            int hi = sd + static_cast<int>(posTexts);
            if (hi > decompressed.size() || hi <= sd) hi = decompressed.size();
            struct SnowRepoint { QByteArray find; char addr; char bit; const char* what; };
            const SnowRepoint reps[] = {
                { QByteArray::fromHex("1410820109"), static_cast<char>(0x46), 0x02, "Snowboard" },
                { QByteArray::fromHex("1410820609"), static_cast<char>(0x45), 0x04, "Glacier Map" },
            };
            int patched = 0;
            for (const SnowRepoint& r : reps) {
                int at = sd;
                while ((at = decompressed.indexOf(r.find, at)) >= 0 && at < hi) {
                    decompressed[at + 2] = r.addr;   // addr 0x82 -> key-item byte
                    decompressed[at + 3] = r.bit;    // story bit -> key-item bit
                    at += r.find.size();
                    patched++;
                }
            }
            if (patched) {
                totalMods++;
                debugStream << "  SNOW_ACCESS: snow re-gated " << patched
                            << " snowboard/glacier check(s) on key-item inventory\n";
            }
        }
    }

    // --- Free Roam diagnostics (disabled): the Rocket Town soft-lock was traced
    //     to the rckt/rckt2 'cloud' init gating UC(disable control)+MENU2 on
    //     Var[3][130] bit 3 (the first-visit intro flag), now pre-set in the
    //     md1stin injection above. dumpFieldScripts() is kept for future use.
    //     To re-enable, dump fields whose lowercased name startsWith("rckt"/"rkt").

    // --- Archipelago mode vs. normal randomization -------------------------
    bool apMode = m_parent && m_parent->m_config.isFeatureEnabled(Config::ArchipelagoIntegration);

    // --- STITM (items) ------------------------------------------------------
    // Key item BITONs are already written, so scan won't match those offsets.
    QVector<STITMInfo> stitmCandidates = scanForSTITM(decompressed, fieldName, debugStream);

    // Collect valid candidates first
    QVector<int> validIndices;
    for (int idx = 0; idx < stitmCandidates.size(); ++idx) {
        if (validateSTITM(stitmCandidates[idx]))
            validIndices.append(idx);
    }

    if (apMode) {
        // md1stin has multiple entity copies of 2 logical pickups (v%2 pattern).
        // All even-indexed copies share BITON A; all odd-indexed copies share BITON B.
        if (fieldName.toLower() == "md1stin" && validIndices.size() >= 2) {
            // Each parity slot caches the (bankByte, addr, bit) we just wrote
            // so subsequent copies of the same logical pickup share the same
            // BITON.  bankByte is sourced from byte 1 of the rewritten op so
            // we stay consistent with whatever bank applySTITMAsArchipelago
            // resolved from the JSON (could be bank 1 or bank 3).
            struct ParityBiton { quint8 bankByte; quint8 addr; quint8 bit; };
            QMap<int, ParityBiton> bitonByParity; // 0=even, 1=odd
            for (int v = 0; v < validIndices.size(); ++v) {
                int parity = v % 2;
                STITMInfo& info = stitmCandidates[validIndices[v]];
                if (!bitonByParity.contains(parity)) {
                    if (applySTITMAsArchipelago(info, decompressed, fieldName, debugStream)) {
                        ParityBiton pb;
                        pb.bankByte = static_cast<quint8>(static_cast<unsigned char>(decompressed[info.offset + 1]));
                        pb.addr     = static_cast<quint8>(static_cast<unsigned char>(decompressed[info.offset + 2]));
                        pb.bit      = static_cast<quint8>(static_cast<unsigned char>(decompressed[info.offset + 3]));
                        bitonByParity[parity] = pb;
                        totalMods++;
                    }
                } else {
                    const ParityBiton& pb = bitonByParity[parity];
                    if (info.offset + STITM_SIZE <= decompressed.size()) {
                        decompressed[info.offset]     = static_cast<char>(BITON_OPCODE);
                        decompressed[info.offset + 1] = static_cast<char>(pb.bankByte);
                        decompressed[info.offset + 2] = static_cast<char>(pb.addr);
                        decompressed[info.offset + 3] = static_cast<char>(pb.bit);
                        decompressed[info.offset + 4] = static_cast<char>(0x5F);
                        totalMods++;
                        debugStream << "  AP_STITM @" << info.offset
                                    << "  (md1stin copy parity=" << parity << ") "
                                    << getItemName(info.originalItemID)
                                    << " -> reusing BITON bank=" << ((pb.bankByte >> 4) & 0x0F)
                                    << " addr=0x" << QString::number(pb.addr, 16)
                                    << " bit=" << pb.bit << "\n";
                    }
                }
            }
        } else {
            // Archipelago mode: replace each STITM with a unique BITON from the queue
            for (int idx : validIndices) {
                STITMInfo& info = stitmCandidates[idx];
                if (applySTITMAsArchipelago(info, decompressed, fieldName, debugStream))
                    totalMods++;
            }
        }
    } else {
        // Normal randomization
        bool isMd1stin = (fieldName.toLower() == "md1stin");
        bool isMktW    = (fieldName.toLower() == "mkt_w");

        if (isMd1stin && validIndices.size() >= 2) {
            quint16 sharedItems[2] = { getRandomItem(1), getRandomItem(1) };
            debugStream << "  md1stin special: syncing all entities to items "
                        << getItemName(sharedItems[0]) << " (" << sharedItems[0] << ") and "
                        << getItemName(sharedItems[1]) << " (" << sharedItems[1] << ")\n";
            for (int v = 0; v < validIndices.size(); ++v) {
                STITMInfo& info = stitmCandidates[validIndices[v]];
                quint16 newItemID = sharedItems[v % 2];
                if (applySTITMRandomization(info, decompressed, newItemID, debugStream)) {
                    modifications.append(OpcodeModification(info.offset, getItemName(newItemID), false));
                    totalMods++;
                }
            }
        } else if (isMktW && validIndices.size() >= 2) {
            QVector<quint16> sharedItems;
            for (int i = 0; i < validIndices.size(); ++i)
                sharedItems.append(getRandomItem(1));
            debugStream << "  mkt_w special: syncing all entities to items\n";
            for (int v = 0; v < validIndices.size(); ++v) {
                STITMInfo& info = stitmCandidates[validIndices[v]];
                quint16 newItemID = sharedItems[v % sharedItems.size()];
                if (applySTITMRandomization(info, decompressed, newItemID, debugStream)) {
                    modifications.append(OpcodeModification(info.offset, getItemName(newItemID), false));
                    totalMods++;
                }
            }
        } else {
            for (int idx : validIndices) {
                STITMInfo& info = stitmCandidates[idx];
                quint16 newItemID = getRandomItem(1);
                if (applySTITMRandomization(info, decompressed, newItemID, debugStream)) {
                    modifications.append(OpcodeModification(info.offset, getItemName(newItemID), false));
                    totalMods++;
                }
            }
        }
    }

    // --- SMTRA (materia) ----------------------------------------------------
    QVector<SMTRAInfo> smtraCandidates = scanForSMTRA(decompressed, fieldName, debugStream);
    for (SMTRAInfo& info : smtraCandidates) {
        if (!validateSMTRA(info)) continue;
        if (apMode) {
            if (applySMTRAAsArchipelago(info, decompressed, fieldName, debugStream))
                totalMods++;
        } else {
            quint8 newMateriaID = getRandomMateria();
            if (applySMTRARandomization(info, decompressed, newMateriaID, debugStream)) {
                modifications.append(OpcodeModification(info.offset, getMateriaName(newMateriaID), true));
                totalMods++;
            }
        }
    }

    // --- Vanilla BITON replacement for Key Items in AP mode -----------------
    if (apMode) {
        int vanillaMods = replaceVanillaBitonsForAP(decompressed, fieldName, debugStream);
        if (vanillaMods > 0) {
            totalMods += vanillaMods;
        }
    }

    // --- mktpb old-man visibility patch (AP mode only) ----------------------
    // Vanilla mktpb init runs:
    //   Var[5][16] = 0
    //   if $KeyItems bit 0 (Cotton Dress)  -> Var[5][16] |= 1
    //   if $KeyItems bit 1 (Satin Dress)   -> Var[5][16] |= 1
    //   if $KeyItems bit 2 (Silk Dress)    -> Var[5][16] |= 1
    // Var[5][16] != 0 hides the old man who hands out the Pharmacy Coupon.
    // When AP delivers a dress remotely the key-item bit at 0x40 is set, so
    // the old man permanently disappears on the next mktpb entry, soft-locking
    // the disguise quest.  NOP the three "Var[5][16] |= 1" BITONs so the
    // initial "Var[5][16] = 0" stands and the old man stays visible
    // regardless of the player's dress inventory.
    if (apMode && fieldName.toLower() == "mktpb") {
        // The "Var[5][16] |= 1" instructions use OR (0x91), not BITON.
        // OpcodeBinaryOperation layout:
        //   [0] 0x91 opcode (OR, 8-bit)
        //   [1] banks: dest var bank=5 (high nibble) | value src bank=0 -> 0x50
        //   [2] var address = 0x10  (decimal 16)
        //   [3] value       = 0x01
        static const char kOldManHidePattern[4] = {
            static_cast<char>(0x91), 0x50, 0x10, 0x01
        };
        int patchCount = 0;
        for (int i = 0; i + 4 <= decompressed.size(); ++i) {
            if (decompressed[i]     == kOldManHidePattern[0] &&
                decompressed[i + 1] == kOldManHidePattern[1] &&
                decompressed[i + 2] == kOldManHidePattern[2] &&
                decompressed[i + 3] == kOldManHidePattern[3]) {
                decompressed[i]     = static_cast<char>(0x5F); // NOP x4
                decompressed[i + 1] = static_cast<char>(0x5F);
                decompressed[i + 2] = static_cast<char>(0x5F);
                decompressed[i + 3] = static_cast<char>(0x5F);
                debugStream << "  AP_MKTPB old-man patch @" << i
                            << ": NOP'd BITON Var[5][16] |= 1\n";
                ++patchCount;
                i += 3; // skip past matched bytes
            }
        }
        if (patchCount > 0) {
            debugStream << "  AP_MKTPB: patched " << patchCount
                        << " old-man-hide BITON(s)\n";
            totalMods += patchCount;
        } else {
            debugStream << "  AP_MKTPB WARN: expected old-man-hide pattern "
                           "(82 50 10 00) not found in mktpb script\n";
        }
    }

    // --- update dialog texts to reflect randomized pickups ------------------
    // Sort by offset so the closest-MESSAGE search assigns correctly
    if (!modifications.isEmpty()) {
        std::sort(modifications.begin(), modifications.end(),
                  [](const OpcodeModification& a, const OpcodeModification& b) {
                      return a.opcodeOffset < b.opcodeOffset;
                  });
        updateFieldTexts(decompressed, modifications, debugStream);
    }

    // --- recompress if anything changed -------------------------------------
    if (totalMods > 0) {
        QByteArray recompressed = LZS::compressWithHeader(decompressed);
        recompressed.detach();   // own buffer, not LZS's static cache
        if (recompressed.isEmpty()) {
            debugStream << fieldName << ": LZS recompression failed!\n";
            return false;
        }
        // ff7tk's LZS compressor corrupts some large/complex fields (e.g. convil_2,
        // the Fort Condor minigame field): its output round-trips through ITS OWN
        // decoder but the GAME decodes it to garbage and crashes when the post-minigame
        // cutscene plays. So verify with a GAME-COMPATIBLE decoder (ff7LzsDecompress),
        // NOT LZS::decompressAllWithHeader (which always agrees with ff7tk's compressor).
        // If ff7tk's output fails, recompress with our own verified encoder; as a last
        // resort leave the field VANILLA rather than ship a corrupt one.
        if (ff7LzsDecompress(recompressed) != decompressed) {
            QByteArray alt = ff7LzsCompressWithHeader(decompressed);
            if (!alt.isEmpty() && ff7LzsDecompress(alt) == decompressed) {
                recompressed = alt;
                debugStream << "  " << fieldName
                            << ": ff7tk LZS game-incompatible — used in-tree encoder ("
                            << recompressed.size() << " bytes)\n";
            } else {
                debugStream << "  " << fieldName
                            << ": LZS recompress corrupt (both encoders) — left VANILLA\n";
                return false;   // keep original fieldData (caller writes it unchanged)
            }
        }
        fieldData = recompressed;
        debugStream << "  >> " << fieldName << ": modified "
                    << totalMods << " opcode(s)\n\n";
    }
    return totalMods > 0;
}

// ============================================================================
// injectFreeRoamMapJump
//
// Overwrites the first 10 bytes of entity 0, script 0 in md1stin with a
// MAPJUMP to wm1 (field ID 2 = outside Kalm) on new game start.
//
// MAPJUMP opcode (0x60) layout - 10 bytes total (9 operand bytes, per
// PyFF7 / Makou opcode table where mjump = 9 args):
//   [0]    0x60  opcode
//   [1-2]  field ID    uint16 LE
//   [3-4]  X           int16 LE   (ignored for wm* dummy fields)
//   [5-6]  Y           int16 LE   (ignored for wm* dummy fields)
//   [7-8]  triangle ID uint16 LE  (ignored for wm* dummy fields)
//   [9]    direction   uint8      (ignored for wm* dummy fields)
//   [10]   0x00 RET    appended so script 0 halts cleanly after the field
//                      change is queued (prevents executing leftover bytes
//                      from the opcodes we partially overwrote).
//
// Field ID 0x002 = wm1 (Outside Kalm). The WM engine reads which wm* field
// you jumped from (Special Variable 6) and sets world map coordinates from
// its own script table - X/Y/triangle/direction in the MAPJUMP bytes are
// ignored for wm* dummy fields.
// ============================================================================

// Returns the total byte length (including the opcode byte) of the FF7 field
// script opcode at `pos`, or -1 if the opcode is invalid/unknown or would run
// past the end of the buffer. Operand counts are from the standard FF7 opcode
// table (cf. PyFF7 / Makou Reactor). SPECIAL (0x0F) and KAWAI (0x28) are
// variable length and handled explicitly.
static int fieldOpcodeLength(const QByteArray& d, int pos, int fileSize)
{
    // Operand byte counts (excluding the 1-byte opcode). -1 = invalid opcode.
    static const int kOperands[256] = {
        /*00*/  0, 2, 2, 2, 2, 2, 2, 1,  1,14, 5, 5,-1,-1, 1, 0,
        /*10*/  1, 2, 1, 2, 5, 6, 7, 8,  7, 8,-1,-1,-1,-1,-1,-1,
        /*20*/ 10, 1, 4, 2, 2, 8, 1, 1,  0, 0, 1, 1, 4, 6, 1, 9,
        /*30*/  3, 3, 3, 1, 1, 3, 4, 7,  5, 5, 5, 3, 0, 0, 0, 0,
        /*40*/  2, 4, 5, 1,-1, 4,-1, 4,  6, 3, 1, 1,-1, 4,-1, 4,
        /*50*/  9, 5, 3, 1, 1, 2, 6, 6,  4, 4, 4, 6, 7, 9, 7, 0,
        /*60*/  9, 1, 4, 5, 5, 0, 8, 0,  8, 1, 6, 8, 0, 3, 2, 5,
        /*70*/  3, 1, 2, 3, 3, 7, 3, 4,  3, 4, 2, 2, 2, 2, 1, 2,
        /*80*/  3, 4, 3, 3, 3, 3, 4, 3,  4, 3, 4, 3, 4, 3, 4, 3,
        /*90*/  4, 3, 4, 3, 4, 2, 2, 2,  2, 2, 3, 4, 5, 6, 6,10,
        /*a0*/  1, 1, 2, 2, 1,10, 8, 8,  5, 5, 1, 3, 0, 5, 2, 2,
        /*b0*/  4, 4, 3, 2, 5, 5, 1, 3,  4, 3, 2, 4, 4, 3,-1, 1,
        /*c0*/ 10, 7,14,11, 0, 2, 2, 1,  1, 1, 3, 2, 2, 2, 1, 1,
        /*d0*/ 12, 1, 1,15, 9, 9, 3, 3,  2, 0,14, 1, 3, 0, 0,10,
        /*e0*/  3, 3, 2, 2, 2, 4, 4, 4,  6, 9, 9, 4, 4, 7, 7,10,
        /*f0*/  1, 4,13, 1, 1, 1, 1, 3,  1, 0, 2, 1, 1, 5, 2, 0,
    };

    if (pos < 0 || pos >= fileSize)
        return -1;
    quint8 op = static_cast<quint8>(d.at(pos));

    if (op == 0x0F) {  // SPECIAL: 2-byte header (0x0F + sub) + sub operands
        if (pos + 1 >= fileSize)
            return -1;
        quint8 sub = static_cast<quint8>(d.at(pos + 1));
        int subOps;
        switch (sub) {
            case 0xF5: subOps = 1; break;  // arrow
            case 0xF6: subOps = 4; break;  // pname
            case 0xF7: subOps = 2; break;  // gmspd
            case 0xF8: subOps = 2; break;  // smspd
            case 0xF9: subOps = 0; break;  // flmat
            case 0xFA: subOps = 0; break;  // flitm
            case 0xFB: subOps = 1; break;  // btlck
            case 0xFC: subOps = 1; break;  // mvlck
            case 0xFD: subOps = 2; break;  // spcnm
            case 0xFE: subOps = 0; break;  // rsglb
            case 0xFF: subOps = 0; break;  // clitm
            default:   return -1;
        }
        int len = 2 + subOps;
        return (pos + len <= fileSize) ? len : -1;
    }

    if (op == 0x28) {  // KAWAI: total length is encoded in the second byte
        if (pos + 1 >= fileSize)
            return -1;
        int len = static_cast<quint8>(d.at(pos + 1));
        if (len < 2)
            return -1;
        return (pos + len <= fileSize) ? len : -1;
    }

    int ops = kOperands[op];
    if (ops < 0)
        return -1;
    int len = 1 + ops;
    return (pos + len <= fileSize) ? len : -1;
}

// NOP every real PMVIE (0xF8, set movie) and MOVIE (0xF9, play movie) opcode in
// a field's section-0 entity scripts. Walks each entity's 32 script entry points
// with fieldOpcodeLength so operand bytes (and false 0xF8/0xF9 inside the offset
// tables / data) are never mistaken for opcodes — and does NOT stop at a RET, so
// it reaches the director's S0-Main (which holds the opening movie) that sits past
// the S0-Init RET in the same slot. 0x5F is a valid 1-byte opcode
// (kOperands[0x5F]==0), so PMVIE (2 bytes -> 5F 5F) and MOVIE (1 byte -> 5F) are
// length-preserving and the walk stays aligned. Idempotent: a re-run sees 0x5F,
// not 0xF8/0xF9, and does nothing. Returns the number of opcodes NOP'd. Mirrors
// the structure of dumpFieldScripts().
static int nopFieldScriptMovies(QByteArray& d, const QString& fieldName, QTextStream& dbg)
{
    const int fileSize = d.size();
    const int HEADER_SIZE = 6 + 9 * 4;
    if (fileSize < HEADER_SIZE) return 0;

    quint32 sectionPositions[9];
    memcpy(sectionPositions, d.constData() + 6, 9 * 4);
    int sec0DataStart = static_cast<int>(sectionPositions[0]) + 4;
    if (sec0DataStart + 32 > fileSize) return 0;

    quint8  nbEntities    = static_cast<quint8>(d.at(sec0DataStart + 2));
    quint16 wStringOffset = 0, nAkaoOffsets = 0;
    memcpy(&wStringOffset, d.constData() + sec0DataStart + 4, 2);
    memcpy(&nAkaoOffsets,  d.constData() + sec0DataStart + 6, 2);
    if (nbEntities == 0) return 0;

    int namesStart       = sec0DataStart + 32;
    int akaoTableStart   = namesStart + 8 * static_cast<int>(nbEntities);
    int offsetTableStart = akaoTableStart + 4 * static_cast<int>(nAkaoOffsets);
    if (offsetTableStart + 64 * static_cast<int>(nbEntities) > fileSize) return 0;

    int walkEnd = sec0DataStart + static_cast<int>(wStringOffset);
    if (nAkaoOffsets > 0 && akaoTableStart + 4 <= fileSize) {
        quint32 firstAkao = 0;
        memcpy(&firstAkao, d.constData() + akaoTableStart, 4);
        int akaoAbs = sec0DataStart + static_cast<int>(firstAkao);
        if (akaoAbs > offsetTableStart && akaoAbs < walkEnd) walkEnd = akaoAbs;
    }
    if (walkEnd > fileSize || walkEnd <= offsetTableStart) walkEnd = fileSize;

    int nopped = 0;
    QSet<quint16> seen;
    for (int e = 0; e < static_cast<int>(nbEntities); ++e) {
        int tbl = offsetTableStart + 64 * e;
        quint16 slot[32];
        memcpy(slot, d.constData() + tbl, 64);
        for (int s = 0; s < 32; ++s) {
            if (seen.contains(slot[s])) continue;
            seen.insert(slot[s]);
            int pos = sec0DataStart + static_cast<int>(slot[s]);
            int guard = 0;
            while (pos >= 0 && pos < walkEnd && guard++ < 4000) {
                quint8 op = static_cast<quint8>(d.at(pos));
                int len = fieldOpcodeLength(d, pos, fileSize);
                if (len <= 0) break;
                if (op == 0xF8 && pos + 1 < fileSize) {        // PMVIE (set movie)
                    d[pos]     = static_cast<char>(0x5F);
                    d[pos + 1] = static_cast<char>(0x5F);
                    ++nopped;
                } else if (op == 0xF9) {                        // MOVIE (play)
                    d[pos] = static_cast<char>(0x5F);
                    ++nopped;
                }
                // NOTE: do NOT break on RET (0x00). The director's S0-Main — which
                // holds the opening PMVIE/MOVIE — lives AFTER the S0-Init RET inside
                // the same slot-0 bytecode region (Makou shows "S0-Init"/"S0-Main"
                // as two halves of one slot split at that RET). Breaking here would
                // stop in S0-Init and never reach the movie. Walking across RET is
                // exactly how injectFreeRoamMapJump() reaches PRTYE further down the
                // same Main script; walkEnd keeps us inside the opcode region.
                pos += len;
            }
        }
    }
    if (nopped)
        dbg << "  MOVIE_NOP: " << fieldName << " NOP'd " << nopped
            << " PMVIE/MOVIE opcode(s)\n";
    return nopped;
}

// NOP every SPLIT (0x09) opcode in a field's section-0 scripts. SPLIT walks the
// non-leader party members to fixed coordinates and BLOCKS until each arrives;
// with a reduced party (Free Roam can have a single character) the empty slots
// 2/3 never arrive -> infinite wait -> softlock (the Forgotten Capital inn sleep
// cutscene). NOPing the opcode + its 14 operand bytes to 0x5F (a valid 1-byte
// no-op) lets the cutscene proceed; the only cost is cosmetic (members aren't
// repositioned). Walks with the opcode-length table so data bytes that happen to
// be 0x09 are never touched. Length-preserving, idempotent.
static int nopFieldScriptSplits(QByteArray& d, const QString& fieldName, QTextStream& dbg)
{
    const int fileSize = d.size();
    const int HEADER_SIZE = 6 + 9 * 4;
    if (fileSize < HEADER_SIZE) return 0;
    quint32 sp[9]; memcpy(sp, d.constData() + 6, 36);
    int sd = static_cast<int>(sp[0]) + 4;
    if (sd + 32 > fileSize) return 0;
    quint8 nb = static_cast<quint8>(d.at(sd + 2));
    quint16 wstr = 0, nak = 0;
    memcpy(&wstr, d.constData() + sd + 4, 2);
    memcpy(&nak,  d.constData() + sd + 6, 2);
    if (nb == 0) return 0;
    int names = sd + 32, akao = names + 8 * nb, offt = akao + 4 * nak;
    if (offt + 64 * nb > fileSize) return 0;
    int walkEnd = sd + static_cast<int>(wstr);
    if (nak > 0 && akao + 4 <= fileSize) {
        quint32 fa = 0; memcpy(&fa, d.constData() + akao, 4);
        int aa = sd + static_cast<int>(fa);
        if (aa > offt && aa < walkEnd) walkEnd = aa;
    }
    if (walkEnd > fileSize || walkEnd <= offt) walkEnd = fileSize;

    int nopped = 0;
    QSet<quint16> seen;
    for (int e = 0; e < static_cast<int>(nb); ++e) {
        quint16 slot[32]; memcpy(slot, d.constData() + offt + 64 * e, 64);
        for (int s = 0; s < 32; ++s) {
            if (seen.contains(slot[s])) continue;
            seen.insert(slot[s]);
            int pos = sd + static_cast<int>(slot[s]), g = 0;
            while (pos < walkEnd && g++ < 4000) {
                quint8 op = static_cast<quint8>(d.at(pos));
                int len = fieldOpcodeLength(d, pos, fileSize);
                if (len <= 0) break;
                if (op == 0x09) {                       // SPLIT -> NOP all bytes
                    for (int k = 0; k < len; ++k) d[pos + k] = static_cast<char>(0x5F);
                    ++nopped;
                } else if (op == 0x00) {
                    break;
                }
                pos += len;
            }
        }
    }
    if (nopped)
        dbg << "  SPLIT_NOP: " << fieldName << " NOP'd " << nopped << " SPLIT opcode(s)\n";
    return nopped;
}

// Neuter one entity's script down to its var write. The Forgotten Capital inn
// (losinn) sleep cutscene lives in entity "init", script slot 4 ("S4 - Go"): it
// runs REQEW execs on "character #2 in the current party" with "wait for end of
// execution". A single-character Free Roam party has no member #2, so the wait
// never returns -> hard softlock. We NOP the whole script to 0x5F EXCEPT the
// BITON (the "Var[3][132] bit 3" cutscene-done flag) and the terminating RET, so
// the flag is still set but nothing blocks. (The cosmetic sleep animation is lost;
// the inn no longer plays its cutscene, which is the intended trade to unblock a
// solo party.) Walks with the opcode-length table; length-preserving + idempotent.
static int neuterInnGoScript(QByteArray& d, const QString& fieldName,
                             const QByteArray& entityName,
                             quint8 keepAddr, quint8 keepBit,
                             QTextStream& dbg)
{
    const int fileSize = d.size();
    const int HEADER_SIZE = 6 + 9 * 4;
    if (fileSize < HEADER_SIZE) return 0;
    quint32 sp[9]; memcpy(sp, d.constData() + 6, 36);
    int sd = static_cast<int>(sp[0]) + 4;
    if (sd + 32 > fileSize) return 0;
    quint8 nb = static_cast<quint8>(d.at(sd + 2));
    quint16 wstr = 0, nak = 0;
    memcpy(&wstr, d.constData() + sd + 4, 2);
    memcpy(&nak,  d.constData() + sd + 6, 2);
    if (nb == 0) return 0;
    int names = sd + 32, akao = names + 8 * nb, offt = akao + 4 * nak;
    if (offt + 64 * nb > fileSize) return 0;
    int walkEnd = sd + static_cast<int>(wstr);
    if (nak > 0 && akao + 4 <= fileSize) {
        quint32 fa = 0; memcpy(&fa, d.constData() + akao, 4);
        int aa = sd + static_cast<int>(fa);
        if (aa > offt && aa < walkEnd) walkEnd = aa;
    }
    if (walkEnd > fileSize || walkEnd <= offt) walkEnd = fileSize;

    // Find the entity by its (NUL-padded, 8-byte) name.
    int entIdx = -1;
    for (int e = 0; e < static_cast<int>(nb); ++e) {
        QByteArray nm(d.constData() + names + 8 * e, 8);
        int z = nm.indexOf('\0'); if (z >= 0) nm.truncate(z);
        if (nm == entityName) { entIdx = e; break; }
    }
    if (entIdx < 0) {
        dbg << "  INN_NEUTER: " << fieldName << " entity '"
            << QString::fromLatin1(entityName) << "' not found\n";
        return 0;
    }

    // Locate the script by CONTENT, not slot number: scan every script slot of the
    // entity for the BITON that sets the target var (the inn "cutscene done" flag),
    // and neuter that script. Makou's "S<N>" label is NOT slot N (S0-Init + S0-Main
    // are two halves of slot 0), so a hard-coded slot is unreliable.
    quint16 slot[32];
    memcpy(slot, d.constData() + offt + 64 * entIdx, 64);
    int targetStart = -1;
    QSet<quint16> seen;
    for (int s = 0; s < 32 && targetStart < 0; ++s) {
        if (seen.contains(slot[s])) continue;
        seen.insert(slot[s]);
        int pos = sd + static_cast<int>(slot[s]), g = 0;
        while (pos >= 0 && pos < walkEnd && g++ < 4000) {
            quint8 op = static_cast<quint8>(d.at(pos));
            int len = fieldOpcodeLength(d, pos, fileSize);
            if (len <= 0) break;
            if (op == 0x00) break;                   // RET: end of this script
            if (op == 0x82 && pos + 3 < fileSize &&
                static_cast<quint8>(d.at(pos + 2)) == keepAddr &&
                static_cast<quint8>(d.at(pos + 3)) == keepBit) {
                targetStart = sd + static_cast<int>(slot[s]);
                break;
            }
            pos += len;
        }
    }
    if (targetStart < 0) {
        dbg << "  INN_NEUTER: " << fieldName << " entity '"
            << QString::fromLatin1(entityName) << "' — BITON addr=0x"
            << QString::number(keepAddr, 16) << " bit=" << keepBit << " not found\n";
        return 0;
    }

    // Neuter that script: NOP every opcode to 0x5F EXCEPT BITONs (var writes) and
    // the terminating RET, so the cutscene-done flag is still set but nothing blocks.
    int pos = targetStart, g = 0, nopped = 0, kept = 0;
    while (pos >= 0 && pos < walkEnd && g++ < 4000) {
        quint8 op = static_cast<quint8>(d.at(pos));
        int len = fieldOpcodeLength(d, pos, fileSize);
        if (len <= 0) break;
        if (op == 0x00) break;                       // RET: keep, end of script
        if (op == 0x82) { ++kept; pos += len; continue; }  // BITON: keep the var line
        for (int k = 0; k < len; ++k) d[pos + k] = static_cast<char>(0x5F);
        ++nopped;
        pos += len;
    }
    dbg << "  INN_NEUTER: " << fieldName << " entity '"
        << QString::fromLatin1(entityName) << "' @" << targetStart
        << " — NOP'd " << nopped << " opcode(s), kept " << kept << " BITON(s)\n";
    return nopped;
}

bool FieldPickupRandomizer_ff7tk::injectFreeRoamMapJump(
    QByteArray& decompressed,
    const QString& fieldName,
    QTextStream& debugStream)
{
    debugStream << "  MAPJUMP_DBG: enter, fileSize=" << decompressed.size() << "\n";
    const int fileSize = decompressed.size();
    const int HEADER_SIZE = 6 + 9 * 4;
    if (fileSize < HEADER_SIZE) {
        debugStream << "  MAPJUMP_DBG: too small\n";
        return false;
    }

    quint32 sectionPositions[9];
    memcpy(sectionPositions, decompressed.constData() + 6, 9 * 4);

    quint32 sec0 = sectionPositions[0];
    debugStream << "  MAPJUMP_DBG: sec0=" << sec0 << "\n";
    if (sec0 + 4 >= static_cast<quint32>(fileSize)) {
        debugStream << "  MAPJUMP_DBG: sec0 out of range\n";
        return false;
    }

    int sec0DataStart = static_cast<int>(sec0) + 4;
    debugStream << "  MAPJUMP_DBG: sec0DataStart=" << sec0DataStart << "\n";
    if (sec0DataStart + 8 > fileSize) {
        debugStream << "  MAPJUMP_DBG: sec0DataStart+8 out of range\n";
        return false;
    }

    // FF7SCRIPTHEADER layout (all offsets from sec0DataStart):
    //   +0  u16 unknown1
    //   +2  u8  nEntities
    //   +3  u8  nModels
    //   +4  u16 wStringOffset
    //   +6  u16 nAkaoOffsets
    //   +8..+31 scale + blanks + creator + name  (24 bytes)
    //   = 32 bytes fixed header
    //   then: szEntities[nEntities][8]
    //   then: dwAkaoOffsets[nAkaoOffsets] (u32 each = 4 bytes)
    //   then: vEntityScripts[nEntities][32] (u16 each)
    if (sec0DataStart + 8 > fileSize) return false;
    quint8  nbEntities   = static_cast<quint8>(decompressed.at(sec0DataStart + 2));
    quint16 nAkaoOffsets = 0;
    memcpy(&nAkaoOffsets, decompressed.constData() + sec0DataStart + 6, 2);
    debugStream << "  MAPJUMP_DBG: nbEntities=" << nbEntities
                << " nAkaoOffsets=" << nAkaoOffsets << "\n";
    if (nbEntities == 0) {
        debugStream << "  MAPJUMP_DBG: nbEntities==0\n";
        return false;
    }

    // Script offset table: 32-byte fixed + 8*N entity names + 4*nAkao Akao offsets
    int offsetTableStart = sec0DataStart + 32
                         + 8 * static_cast<int>(nbEntities)
                         + 4 * static_cast<int>(nAkaoOffsets);
    debugStream << "  MAPJUMP_DBG: offsetTableStart=" << offsetTableStart << "\n";
    if (offsetTableStart + 2 > fileSize) {
        debugStream << "  MAPJUMP_DBG: offsetTableStart out of range\n";
        return false;
    }

    // Entity 0, script 0 is the first u16; offsets are relative to sec0DataStart
    quint16 script0RelOffset;
    memcpy(&script0RelOffset, decompressed.constData() + offsetTableStart, 2);
    debugStream << "  MAPJUMP_DBG: script0RelOffset=" << script0RelOffset << "\n";

    int script0AbsStart = sec0DataStart + static_cast<int>(script0RelOffset);
    debugStream << "  MAPJUMP_DBG: script0AbsStart=" << script0AbsStart << "\n";
    if (script0AbsStart < 0 || script0AbsStart >= fileSize) {
        debugStream << "  MAPJUMP_DBG: script0AbsStart out of range\n";
        return false;
    }

    // The opening "New party: Cloud" lives in the director's Main script, not
    // its short Init (script 0). All entities' scripts are packed contiguously
    // in section 0, so walk the whole bytecode region. Bound the walk at the
    // start of the AKAO/tutorial blocks (or the string table) so we never parse
    // non-opcode data.
    quint16 wStringOffset = 0;
    memcpy(&wStringOffset, decompressed.constData() + sec0DataStart + 4, 2);
    int walkEnd = sec0DataStart + static_cast<int>(wStringOffset);
    if (nAkaoOffsets > 0) {
        int akaoTableStart = sec0DataStart + 32 + 8 * static_cast<int>(nbEntities);
        if (akaoTableStart + 4 <= fileSize) {
            quint32 firstAkao = 0;
            memcpy(&firstAkao, decompressed.constData() + akaoTableStart, 4);
            int akaoAbs = sec0DataStart + static_cast<int>(firstAkao);
            if (akaoAbs > script0AbsStart && akaoAbs < walkEnd)
                walkEnd = akaoAbs;
        }
    }
    if (walkEnd > fileSize || walkEnd <= script0AbsStart)
        walkEnd = fileSize;
    debugStream << "  MAPJUMP_DBG: walkEnd=" << walkEnd
                << " wStringOffset=" << wStringOffset << "\n";

    // Walk opcodes to find PRTYE (0xCA = "New party"). We inject the MAPJUMP
    // immediately AFTER it so the active party (Cloud) is set up before the
    // engine transfers to the world map - jumping before that crashes FF7.
    // Walk across script boundaries (RET is just another opcode here) until we
    // find the first PRTYE whose first member is Cloud (0x00).
    int pos = script0AbsStart;
    int injectAt = -1;
    int guard = 0;
    while (pos < walkEnd && guard++ < 100000) {
        int len = fieldOpcodeLength(decompressed, pos, fileSize);
        if (len <= 0) {
            debugStream << "  MAPJUMP_DBG: invalid opcode 0x"
                        << QString::number(static_cast<quint8>(decompressed.at(pos)), 16)
                        << " @" << pos << " - aborting injection\n";
            return false;
        }
        quint8 op = static_cast<quint8>(decompressed.at(pos));
        if (op == 0xCA) {  // PRTYE - New party. Confirm first member is Cloud (0x00).
            quint8 m0 = (pos + 1 < fileSize) ? static_cast<quint8>(decompressed.at(pos + 1)) : 0xFF;
            debugStream << "  MAPJUMP_DBG: found PRTYE @" << pos
                        << " member0=" << m0 << "\n";
            if (m0 == 0x00) {
                injectAt = pos + len;  // position right after the party opcode
                break;
            }
        }
        pos += len;
    }

    if (injectAt < 0) {
        debugStream << "  MAPJUMP_DBG: PRTYE (New party: Cloud) not found - aborting\n";
        return false;
    }
    // Injected sequence (after PRTYE, before transferring to the world map).
    // The skipped Midgar intro normally performs all of this setup; on a NEW
    // GAME none of it happens, so we replicate the essentials here:
    //   MENU 6        - Cloud name-entry screen. Sets Cloud's name (default
    //                   "Cloud" instead of the kernel placeholder "EX-SOLDIER")
    //                   and initialises the party's average level.
    //   SETWORD x2    - menu visibility = 0x03FF (all standard commands shown,
    //                   incl. Materia = bit 2) and locking = 0x0000 (none).
    //   SETBYTE       - Kalm conversation flags = 0x03 (NPC-spoken bits) to
    //                   avoid the Kalm progression lock.
    //   SETWORD       - game moment = 1603.
    //   MAPJUMP + RET - transfer to wm1 and halt the script cleanly.
    //
    // Field memory banks (cf. FF7 savemap): bank 1 maps to savemap 0x0BA4.
    //   8-bit bank id 0x1 / 16-bit bank id 0x2.
    //   game moment        = Var[2][0]   (savemap 0x0BA4)
    //   menu visibility    = Var[2][0x1C](savemap 0x0BC0)
    //   menu locking       = Var[2][0x1E](savemap 0x0BC2)
    //   Kalm conv. flags   = Var[1][0x80](savemap 0x0C24)
    // SET* bank byte = (Dest<<4)|Source; Source 0 = write literal value V.
    static constexpr quint8  kMenuBank16    = 0x20;   // dest 16-bit bank 1, literal src
    static constexpr quint8  kMenuBank8     = 0x10;   // dest 8-bit  bank 1, literal src
    static constexpr quint16 kGameMoment    = 1997;
    static constexpr quint16 kMenuVisible   = 0x03FF; // Item..Save all visible
    static constexpr quint16 kMenuLocking   = 0x0000; // nothing locked
    static constexpr quint8  kKalmFlagsAddr = 0x80;   // Var[1][128]
    static constexpr quint8  kKalmFlags     = 0x03;   // bits 0 + 1
    // NOTE: the current disc (savemap 0x0EA4) is NOT field-settable — fields change
    // it via the DSKCG opcode (engine-handled), never a direct SETBYTE. Free Roam's
    // "disc 3" is forced by the client writing 0x0EA4 instead (see FF7Client.py).

    // BITON Var[3][128] bit 1 — marks the psdun_2 (Mythril Mines) line-trigger
    // party-split event as "already played" so Free Roam doesn't fire it and
    // boot the player back. bank 3 = 8-bit half of the 2nd bank pair; the engine
    // resolves the savemap offset, so we only encode the bank nibble here.
    //   banks byte = (addrBank 3 << 4) | (bitSrc 0 = literal) = 0x30
    static constexpr quint8  kBitOnBanks    = 0x30;   // addr bank 3, literal bit
    static constexpr quint8  kFreeRoamFlagAddr = 0x80; // Var[3][128]
    static constexpr quint8  kFreeRoamFlagBit  = 0x01; // "bitON 1" = bit index 1

    // BITON Var[3][130] bit 3 — marks the Rocket Town first-visit intro as
    // already played. The rckt/rckt2 'cloud' init runs
    //   IFUB Var[3][130] bitOFF 3 -> UC(01) [disable control] + MENU2(01)
    // expecting the intro cutscene to re-enable control. On a moment-1603 Free
    // Roam the bit is OFF and that cutscene never fires, soft-locking the player
    // on entry. Setting the bit makes the IFUB take the skip branch.
    static constexpr quint8  kRocketFlagAddr = 0x82; // Var[3][130]
    static constexpr quint8  kRocketFlagBit  = 0x03; // "bitON 3" = bit index 3

    // BITON Var[d][0x50] bit 5 (savemap 0xEA4+0x50 = 0xEF4.5) — arms the Da-chao
    // Turks scene. datiao_1's director gates the Reno/Rude appearance on
    //   IFUB 0xEF4 bitON 5  (scene skipped unless SET)  AND  0x102C bitOFF 0
    // and Free Roam (moment 1997) never sets 0xEF4.5, so the Turks never spawn.
    // The scene is the lead-in to the Yuffie's-house (yufy1) sequence, so seed
    // the bit at new-game. One-time: the scene BITONs 0x102C.0 when it plays, so
    // it self-consumes on first datiao_1 visit. (Same bank byte 0xd0 as Vincent's
    // 0xEF4.2 recruit flag — different bit, no conflict; live-verified 2026-07-16
    // that this single bit is the sole trigger.)
    static constexpr quint8  kDachaoTurksBanks = 0xD0; // addr bank nibble d (0xEA4), literal bit
    static constexpr quint8  kDachaoTurksAddr  = 0x50; // Var[.][0x50] = savemap 0xEF4
    static constexpr quint8  kDachaoTurksBit   = 0x05;

    // MAPJUMP to wm1 (field ID 2 = outside Kalm).
    // X/Y/triangle/direction are ignored by the WM engine for wm* dummy fields.
    static constexpr quint16 kFieldId  = 2;   // wm1 = Outside Kalm
    static constexpr qint16  kSpawnX   = 0;
    static constexpr qint16  kSpawnY   = 0;
    static constexpr quint16 kTriangle = 0;
    static constexpr quint8  kDir      = 0;

    // NOTE: intro music (MUSIC opcode) + a welcome MESSAGE were reverted — they
    // crashed the field right after the intro movie. The field MUSIC index and a
    // blocking MESSAGE in this early (pre-interactive) script context are not safe
    // here without in-game verification (md1stin has only 2 akao entries, so a bad
    // MUSIC index hard-crashes). overwriteFieldDialog() is kept below for a future,
    // tested re-add. This is the proven new-game -> world-map injection.
    auto put16 = [](QByteArray& b, quint16 v) {
        b.append(static_cast<char>(v & 0xFF));
        b.append(static_cast<char>((v >> 8) & 0xFF));
    };

    QByteArray seq;
    // MENU 6 — name entry for Cloud (char id 0); B=0 (literal), T=6, P=0
    seq.append(static_cast<char>(0x49));
    seq.append(static_cast<char>(0x00));
    seq.append(static_cast<char>(0x06));
    seq.append(static_cast<char>(0x00));
    // SETWORD menu visibility (Var[2][0x1C]) = 0x03FF
    seq.append(static_cast<char>(0x81)); seq.append(static_cast<char>(kMenuBank16));
    seq.append(static_cast<char>(0x1C)); put16(seq, kMenuVisible);
    // SETWORD menu locking (Var[2][0x1E]) = 0x0000
    seq.append(static_cast<char>(0x81)); seq.append(static_cast<char>(kMenuBank16));
    seq.append(static_cast<char>(0x1E)); put16(seq, kMenuLocking);
    // SETBYTE Kalm conversation flags (Var[1][0x80]) = 0x03
    seq.append(static_cast<char>(0x80)); seq.append(static_cast<char>(kMenuBank8));
    seq.append(static_cast<char>(kKalmFlagsAddr)); seq.append(static_cast<char>(kKalmFlags));
    // SETWORD game moment (Var[2][0]) = 1997
    seq.append(static_cast<char>(0x81)); seq.append(static_cast<char>(kMenuBank16));
    seq.append(static_cast<char>(0x00)); put16(seq, kGameMoment);
    // BITON Var[3][128] bit 1 — skip psdun_2 (Mythril Mines) party-split trigger
    seq.append(static_cast<char>(0x82)); seq.append(static_cast<char>(kBitOnBanks));
    seq.append(static_cast<char>(kFreeRoamFlagAddr)); seq.append(static_cast<char>(kFreeRoamFlagBit));
    // BITON Var[3][130] bit 3 — skip Rocket Town (rckt/rckt2) entry soft-lock
    seq.append(static_cast<char>(0x82)); seq.append(static_cast<char>(kBitOnBanks));
    seq.append(static_cast<char>(kRocketFlagAddr)); seq.append(static_cast<char>(kRocketFlagBit));
    // BITON Var[d][0x50] bit 5 — arm the Da-chao Turks scene (datiao_1 lead-in
    // to the Yuffie's-house sequence)
    seq.append(static_cast<char>(0x82)); seq.append(static_cast<char>(kDachaoTurksBanks));
    seq.append(static_cast<char>(kDachaoTurksAddr)); seq.append(static_cast<char>(kDachaoTurksBit));
    // MAPJUMP wm1
    seq.append(static_cast<char>(0x60));
    put16(seq, kFieldId);
    put16(seq, static_cast<quint16>(kSpawnX)); put16(seq, static_cast<quint16>(kSpawnY));
    put16(seq, kTriangle);
    seq.append(static_cast<char>(kDir));
    // RET — halt script cleanly after queuing the jump
    seq.append(static_cast<char>(0x00));

    if (injectAt + seq.size() > walkEnd || injectAt + seq.size() > fileSize) {
        debugStream << "  MAPJUMP_DBG: not enough room after PRTYE for "
                    << seq.size() << " bytes - aborting\n";
        return false;
    }
    for (int i = 0; i < seq.size(); ++i)
        decompressed[injectAt + i] = seq.at(i);

    debugStream << "  FREE_ROAM: injected MENU(name)+menu masks+Kalm flags+SETWORD(gameMoment="
                << kGameMoment << ")+BITON+MAPJUMP @" << injectAt << " -> wm1 fieldId="
                << kFieldId << " bytes=" << seq.size() << "\n";
    return true;
}

// ============================================================================
// dumpFieldScripts — decode a field's section-0 entity script table + opcodes.
//   Diagnostic only (writes to the randomization debug log). Used to locate the
//   autonomous entry event that freezes the player (no control) on certain maps
//   at game moment 1603 in Free Roam.
// ============================================================================
namespace {
// Mnemonics for the control-flow / scene opcodes that matter when reading an
// entry event. Anything not listed prints as its hex byte only.
QString ff7OpcodeName(quint8 op)
{
    switch (op) {
    case 0x00: return "RET";
    case 0x01: return "REQ";    case 0x02: return "REQSW";  case 0x03: return "REQEW";
    case 0x04: return "PREQ";   case 0x05: return "PRQSW";  case 0x06: return "PRQEW";
    case 0x07: return "RETTO";
    case 0x10: return "JMPF";   case 0x11: return "JMPFL";
    case 0x12: return "JMPB";   case 0x13: return "JMPBL";
    case 0x14: return "IFUB";   case 0x15: return "IFUBL";
    case 0x16: return "IFSW";   case 0x17: return "IFSWL";
    case 0x18: return "IFUW";   case 0x19: return "IFUWL";
    case 0x24: return "WAIT";
    case 0x25: return "nFADE";  case 0x2F: return "WCLS";
    case 0x30: return "WSIZW";  case 0x36: return "WMODE";
    case 0x38: return "MES?";
    case 0x40: return "MESSAGE";case 0x41: return "MPARA";  case 0x42: return "MPRA2";
    case 0x48: return "ASK";    case 0x49: return "MENU";   case 0x4A: return "MENU2";
    case 0x60: return "MAPJUMP";
    case 0x80: return "SETBYTE";case 0x81: return "SETWORD";
    case 0x82: return "BITON";  case 0x83: return "BITOFF"; case 0x84: return "BITXOR";
    case 0x85: return "PLUS!";  case 0x86: return "PLUS2!";
    case 0x90: return "JMPFF?"; case 0x95: return "MINUS!";
    case 0xA0: return "MOVA";
    case 0xC7: return "SOLID";
    case 0xCA: return "PRTYE";  case 0xCB: return "PRTYA";
    case 0xD8: return "PMVIE";  case 0xD9: return "MOVIE";  case 0xDA: return "MVIEF";
    case 0xE0: return "BGON";   case 0xE1: return "BGOFF";
    case 0xF5: return "AKAO2?"; case 0xFD: return "AKAO";
    default:   return QString();
    }
}
} // namespace

void FieldPickupRandomizer_ff7tk::dumpFieldScripts(
    const QByteArray& decompressed, const QString& fieldName, QTextStream& debugStream)
{
    const int fileSize = decompressed.size();
    const int HEADER_SIZE = 6 + 9 * 4;
    debugStream << "\n=== FIELD_DUMP " << fieldName << " (size=" << fileSize << ") ===\n";
    if (fileSize < HEADER_SIZE) { debugStream << "  too small\n"; return; }

    quint32 sectionPositions[9];
    memcpy(sectionPositions, decompressed.constData() + 6, 9 * 4);
    int sec0DataStart = static_cast<int>(sectionPositions[0]) + 4;
    if (sec0DataStart + 32 > fileSize) { debugStream << "  sec0 out of range\n"; return; }

    quint8  nbEntities   = static_cast<quint8>(decompressed.at(sec0DataStart + 2));
    quint16 wStringOffset = 0, nAkaoOffsets = 0;
    memcpy(&wStringOffset, decompressed.constData() + sec0DataStart + 4, 2);
    memcpy(&nAkaoOffsets,  decompressed.constData() + sec0DataStart + 6, 2);
    if (nbEntities == 0) { debugStream << "  no entities\n"; return; }

    int namesStart       = sec0DataStart + 32;
    int akaoTableStart   = namesStart + 8 * static_cast<int>(nbEntities);
    int offsetTableStart = akaoTableStart + 4 * static_cast<int>(nAkaoOffsets);
    if (offsetTableStart + 64 * static_cast<int>(nbEntities) > fileSize) {
        debugStream << "  script table out of range\n"; return;
    }

    // Walk bound: start of strings or first AKAO block, whichever is first.
    int walkEnd = sec0DataStart + static_cast<int>(wStringOffset);
    if (nAkaoOffsets > 0 && akaoTableStart + 4 <= fileSize) {
        quint32 firstAkao = 0;
        memcpy(&firstAkao, decompressed.constData() + akaoTableStart, 4);
        int akaoAbs = sec0DataStart + static_cast<int>(firstAkao);
        if (akaoAbs > offsetTableStart && akaoAbs < walkEnd) walkEnd = akaoAbs;
    }
    if (walkEnd > fileSize || walkEnd <= offsetTableStart) walkEnd = fileSize;

    debugStream << "  nbEntities=" << nbEntities << " nAkao=" << nAkaoOffsets
                << " walkEnd=" << walkEnd << "\n";

    auto entityName = [&](int e) {
        QByteArray nm(decompressed.constData() + namesStart + 8 * e, 8);
        int z = nm.indexOf('\0'); if (z >= 0) nm.truncate(z);
        return QString::fromLatin1(nm);
    };

    for (int e = 0; e < static_cast<int>(nbEntities); ++e) {
        // Read this entity's 32 script entry offsets (relative to sec0DataStart).
        int tbl = offsetTableStart + 64 * e;
        quint16 slot[32];
        memcpy(slot, decompressed.constData() + tbl, 64);

        debugStream << "  -- Entity " << e << " '" << entityName(e) << "' scripts: ";
        for (int s = 0; s < 32; ++s) debugStream << slot[s] << (s == 31 ? "" : ",");
        debugStream << "\n";

        // Dump each unique script start once, listing the slots that use it.
        QList<quint16> seen;
        for (int s = 0; s < 32; ++s) {
            if (seen.contains(slot[s])) continue;
            seen.append(slot[s]);
            QString users;
            for (int t = 0; t < 32; ++t) if (slot[t] == slot[s]) users += QString::number(t) + " ";
            int start = sec0DataStart + static_cast<int>(slot[s]);
            debugStream << "    [script " << users.trimmed() << "] @rel" << slot[s]
                        << " abs" << start << ":\n";
            if (start < 0 || start >= walkEnd) { debugStream << "      (out of range)\n"; continue; }

            int pos = start, guard = 0;
            while (pos < walkEnd && guard++ < 600) {
                int len = fieldOpcodeLength(decompressed, pos, fileSize);
                if (len <= 0) { debugStream << "      @" << pos << " BAD op\n"; break; }
                quint8 op = static_cast<quint8>(decompressed.at(pos));
                QString name = ff7OpcodeName(op);
                QString operands;
                for (int b = 1; b < len && pos + b < fileSize; ++b)
                    operands += QString("%1 ").arg(static_cast<quint8>(decompressed.at(pos + b)), 2, 16, QChar('0'));
                debugStream << "      @" << pos << " "
                            << QString("%1").arg(op, 2, 16, QChar('0')) << " "
                            << (name.isEmpty() ? QStringLiteral("") : name)
                            << (operands.isEmpty() ? QStringLiteral("") : QStringLiteral("  ") + operands.trimmed())
                            << "\n";
                pos += len;
                if (op == 0x00) break;  // RET ends this script body
            }
        }
    }
    debugStream << "=== END FIELD_DUMP " << fieldName << " ===\n\n";
}

// ============================================================================
// overwriteFieldDialog — repurpose an existing dialog string in place.
//   Field "section 0" header: posTexts (offset to the dialog block, relative to
//   sec0DataStart) lives at sec0DataStart+4. The dialog block is:
//     [u16 count][u16 offset x count][text bytes, each 0xFF-terminated]
//   We pick the longest dialog whose byte span fits the new text + terminator
//   and overwrite it (padding leftover bytes with 0xFF). No bytes move.
// ============================================================================
int FieldPickupRandomizer_ff7tk::overwriteFieldDialog(
    QByteArray& decompressed, const QByteArray& encoded, QTextStream& debugStream)
{
    const int fileSize = decompressed.size();
    if (fileSize < 6 + 9 * 4) return -1;
    quint32 sectionPositions[9];
    memcpy(sectionPositions, decompressed.constData() + 6, 9 * 4);
    const int sec0DataStart = static_cast<int>(sectionPositions[0]) + 4;
    if (sec0DataStart + 8 > fileSize) return -1;

    quint16 wStringOffset = 0;
    memcpy(&wStringOffset, decompressed.constData() + sec0DataStart + 4, 2);
    const int dlgBlock = sec0DataStart + static_cast<int>(wStringOffset);
    if (dlgBlock + 2 > fileSize) return -1;
    quint16 nbDialogs = 0;
    memcpy(&nbDialogs, decompressed.constData() + dlgBlock, 2);
    if (nbDialogs == 0) return -1;

    // Strip any trailing terminators; we append exactly one when writing.
    QByteArray body = encoded;
    while (!body.isEmpty() && static_cast<quint8>(body.back()) == 0xFF) body.chop(1);
    const int needed = body.size() + 1;  // text + one 0xFF terminator

    int bestId = -1, bestStart = -1, bestCap = -1;
    for (int id = 0; id < nbDialogs; ++id) {
        const int ptrPos = dlgBlock + 2 + id * 2;
        if (ptrPos + 2 > fileSize) break;
        quint16 rel = 0;
        memcpy(&rel, decompressed.constData() + ptrPos, 2);
        const int tStart = dlgBlock + static_cast<int>(rel);
        if (tStart >= fileSize || tStart < dlgBlock) continue;
        int tEnd = tStart;
        while (tEnd < fileSize && static_cast<quint8>(decompressed.at(tEnd)) != 0xFF) tEnd++;
        if (tEnd >= fileSize) continue;
        const int cap = (tEnd - tStart) + 1;  // includes the original terminator
        if (cap >= needed && cap > bestCap) { bestCap = cap; bestId = id; bestStart = tStart; }
    }
    if (bestId < 0) {
        debugStream << "  WELCOME: no dialog slot >= " << needed << " bytes — skipping message\n";
        return -1;
    }
    for (int i = 0; i < body.size(); ++i)
        decompressed[bestStart + i] = body.at(i);
    for (int i = body.size(); i < bestCap; ++i)
        decompressed[bestStart + i] = static_cast<char>(0xFF);
    debugStream << "  WELCOME: overwrote dialog #" << bestId
                << " (cap " << bestCap << ", used " << needed << ")\n";
    return bestId;
}

// ============================================================================
// scanForSTITM  –  parse the field file section table (like Makou Reactor)
//                   then scan ONLY section 0 (scripts) for 0x58 opcodes.
//
// FF7 PC field file layout  (from FieldPC.cpp / Field.cpp in Makou):
//   bytes  0- 1 : blank
//   bytes  2- 5 : unknown header data
//   bytes  6-41 : 9 x quint32 section positions (absolute offsets)
//   then 9 sections, each preceded by a 4-byte section-size header.
//
// Script section = section 0.
//   data starts at  sectionPositions[0] + 4
//   data ends   at  sectionPositions[1]       (start of section 1)
// ============================================================================

QVector<STITMInfo> FieldPickupRandomizer_ff7tk::scanForSTITM(
    const QByteArray& fieldData,
    const QString& fieldName,
    QTextStream& debugStream)
{
    QVector<STITMInfo> results;
    const int fileSize = fieldData.size();

    // --- parse section table ------------------------------------------------
    // Need at least the 42-byte header (6 + 9*4)
    const int HEADER_SIZE = 6 + 9 * 4;  // 42 bytes
    if (fileSize < HEADER_SIZE) {
        debugStream << fieldName << ": too small for section table ("
                    << fileSize << " bytes), skipping\n";
        return results;
    }

    quint32 sectionPositions[9];
    memcpy(sectionPositions, fieldData.constData() + 6, 9 * 4);

    // Validate section 0 and section 1 positions
    quint32 sec0 = sectionPositions[0];
    quint32 sec1 = sectionPositions[1];

    if (sec0 + 4 >= static_cast<quint32>(fileSize) ||
        sec1 > static_cast<quint32>(fileSize) ||
        sec1 <= sec0 + 4) {
        debugStream << fieldName << ": invalid section positions (sec0="
                    << sec0 << " sec1=" << sec1
                    << " fileSize=" << fileSize << "), skipping\n";
        return results;
    }

    // Script section data: skip the 4-byte section-size header
    int sec0DataStart = static_cast<int>(sec0) + 4;

    // Read section 0 header to get proper script bytecode bounds
    if (sec0DataStart + 8 > fileSize) return results;
    quint8  nbEntities = static_cast<quint8>(fieldData.at(sec0DataStart + 2));
    quint16 posTexts;
    memcpy(&posTexts, fieldData.constData() + sec0DataStart + 4, 2);

    // Script bytecode starts after: 32-byte fixed header + 8*N entity names + 64*N script offsets
    int headerSize = 32 + 72 * nbEntities;
    int scriptStart = sec0DataStart + headerSize;
    int scriptEnd   = sec0DataStart + posTexts;  // text section starts here

    if (scriptStart >= scriptEnd || scriptEnd > fileSize) return results;

    debugStream << fieldName << ": script section bytes "
                << scriptStart << ".." << scriptEnd
                << " (" << (scriptEnd - scriptStart) << " bytes)\n";

    // --- scan only the script bytecode for STITM (0x58) ---------------------
    for (int i = scriptStart; i <= scriptEnd - STITM_SIZE; ++i) {
        if (static_cast<quint8>(fieldData[i]) != STITM_OPCODE) continue;

        const OpcodeSTITMRaw* raw =
            reinterpret_cast<const OpcodeSTITMRaw*>(fieldData.constData() + i);

        STITMInfo info;
        info.offset          = i;
        info.banks           = raw->banks;
        info.originalItemID  = raw->itemID;   // little-endian on x86
        info.originalQuantity = raw->quantity;
        info.isDirectValue   = (raw->banks == 0x00);

        results.append(info);
    }

    if (!results.isEmpty()) {
        debugStream << "  " << fieldName << ": " << results.size()
                    << " STITM candidate(s) in script section\n";
    }

    return results;
}

// ============================================================================
// validateSTITM  –  conservative rules to avoid false positives
// ============================================================================

bool FieldPickupRandomizer_ff7tk::validateSTITM(const STITMInfo& info) const
{
    // Only randomise direct-value pickups (banks == 0x00).
    // When banks != 0 the item ID / quantity come from game variables and
    // modifying the literal bytes would corrupt the script logic.
    if (!info.isDirectValue) return false;

    // Item ID must be in the valid range (0 .. MAX_ITEM_ID).
    if (info.originalItemID > MAX_ITEM_ID) return false;

    // Quantity must be 1-99 (0 is suspicious, >99 is impossible in-game).
    if (info.originalQuantity < 1 || info.originalQuantity > 99) return false;

    return true;
}

// ============================================================================
// applySTITMRandomization  –  rewrite the itemID in-place
// ============================================================================

bool FieldPickupRandomizer_ff7tk::applySTITMRandomization(
    STITMInfo& info,
    QByteArray& fieldData,
    quint16 newItemID,
    QTextStream& debugStream)
{
    if (info.offset + STITM_SIZE > fieldData.size()) return false;

    // Rewrite the two itemID bytes in-place (little-endian)
    OpcodeSTITMRaw* raw =
        reinterpret_cast<OpcodeSTITMRaw*>(fieldData.data() + info.offset);
    raw->itemID = newItemID;
    // banks and quantity are left untouched

    debugStream << "  STITM @" << info.offset
                << "  " << getItemName(info.originalItemID)
                << " (" << info.originalItemID << ")"
                << " -> " << getItemName(newItemID)
                << " (" << newItemID << ")"
                << "  qty=" << info.originalQuantity << "\n";

    return true;
}

// ============================================================================
// scanForSMTRA  –  scan script section for SMTRA (0x5B) opcodes
// ============================================================================

QVector<SMTRAInfo> FieldPickupRandomizer_ff7tk::scanForSMTRA(
    const QByteArray& fieldData,
    const QString& fieldName,
    QTextStream& debugStream)
{
    QVector<SMTRAInfo> results;
    const int fileSize = fieldData.size();

    const int HEADER_SIZE = 6 + 9 * 4;
    if (fileSize < HEADER_SIZE) return results;

    quint32 sectionPositions[9];
    memcpy(sectionPositions, fieldData.constData() + 6, 9 * 4);

    quint32 sec0 = sectionPositions[0];
    quint32 sec1 = sectionPositions[1];

    if (sec0 + 4 >= static_cast<quint32>(fileSize) ||
        sec1 > static_cast<quint32>(fileSize) ||
        sec1 <= sec0 + 4) {
        return results;
    }

    // Read section 0 header to get proper script bytecode bounds
    int sec0DataStart = static_cast<int>(sec0) + 4;
    if (sec0DataStart + 8 > fileSize) return results;
    quint8  nbEntities = static_cast<quint8>(fieldData.at(sec0DataStart + 2));
    quint16 posTexts;
    memcpy(&posTexts, fieldData.constData() + sec0DataStart + 4, 2);

    // Script bytecode starts after: 32-byte fixed header + 8*N entity names + 64*N script offsets
    int headerSize = 32 + 72 * nbEntities;
    int scriptStart = sec0DataStart + headerSize;
    int scriptEnd   = sec0DataStart + posTexts;

    if (scriptStart >= scriptEnd || scriptEnd > fileSize) return results;

    for (int i = scriptStart; i <= scriptEnd - SMTRA_SIZE; ++i) {
        if (static_cast<quint8>(fieldData[i]) != SMTRA_OPCODE) continue;

        const OpcodeSMTRARaw* raw =
            reinterpret_cast<const OpcodeSMTRARaw*>(fieldData.constData() + i);

        SMTRAInfo info;
        info.offset           = i;
        info.banks[0]         = raw->banks[0];
        info.banks[1]         = raw->banks[1];
        info.originalMateriaID = raw->materiaID;
        info.originalAP[0]    = raw->APCount[0];
        info.originalAP[1]    = raw->APCount[1];
        info.originalAP[2]    = raw->APCount[2];
        info.isDirectValue    = (raw->banks[0] == 0x00 && raw->banks[1] == 0x00);

        results.append(info);
    }

    if (!results.isEmpty()) {
        debugStream << "  " << fieldName << ": " << results.size()
                    << " SMTRA candidate(s) in script section\n";
    }

    return results;
}

// ============================================================================
// validateSMTRA  –  conservative rules for materia opcodes
// ============================================================================

bool FieldPickupRandomizer_ff7tk::validateSMTRA(const SMTRAInfo& info) const
{
    // Only randomise direct-value materia pickups (both banks == 0x00)
    if (!info.isDirectValue) return false;

    // Materia ID must be in valid range (0 .. MAX_MATERIA_ID)
    if (info.originalMateriaID > MAX_MATERIA_ID) return false;

    return true;
}

// ============================================================================
// applySMTRARandomization  –  rewrite the materiaID in-place
// ============================================================================

bool FieldPickupRandomizer_ff7tk::applySMTRARandomization(
    SMTRAInfo& info,
    QByteArray& fieldData,
    quint8 newMateriaID,
    QTextStream& debugStream)
{
    if (info.offset + SMTRA_SIZE > fieldData.size()) return false;

    OpcodeSMTRARaw* raw =
        reinterpret_cast<OpcodeSMTRARaw*>(fieldData.data() + info.offset);
    raw->materiaID = newMateriaID;
    // banks and AP are left untouched

    debugStream << "  SMTRA @" << info.offset
                << "  " << getMateriaName(info.originalMateriaID)
                << " (" << info.originalMateriaID << ")"
                << " -> " << getMateriaName(newMateriaID)
                << " (" << newMateriaID << ")"
                << "  AP=" << info.originalAP[0]
                << "," << info.originalAP[1]
                << "," << info.originalAP[2] << "\n";

    return true;
}

// ============================================================================
// loadApJson  –  read Archipelago output JSON and build (field, item_text)
//               -> (address, bit) lookup queues.
// ============================================================================

bool FieldPickupRandomizer_ff7tk::loadApJson(
    const QString& path,
    QTextStream& debugStream)
{
    const QByteArray seedJson = ApSeedFile::readJson(path);
    if (seedJson.isEmpty()) {
        debugStream << "AP JSON: cannot open " << path << "\n";
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(seedJson);
    if (doc.isNull() || !doc.isObject()) {
        debugStream << "AP JSON: invalid JSON in " << path << "\n";
        return false;
    }

    m_apJsonLookup.clear();
    m_apJsonLastBiton.clear();
    m_apBitonEntries.clear();

    QJsonArray placements = doc.object()["placements"].toArray();
    for (const QJsonValue& v : placements) {
        QJsonObject p = v.toObject();
        int bank    = p["bank"].toInt(-1);
        int address = p["address"].toInt(-1);
        int bit     = p["bit"].toInt(-1);
        if (bank < 0 || address < 0 || bit < 0) continue;

        QString itemText = p["item_text"].toString().toLower().trimmed();
        if (itemText.isEmpty()) continue;

        // Strip "keyitem: " prefix so item_text matches getItemName() output
        if (itemText.startsWith("keyitem: "))
            itemText = itemText.mid(9);

        ApBitonCoord coord{
            static_cast<quint8>(bank),
            static_cast<quint8>(address),
            static_cast<quint8>(bit),
        };

        // Register the detection coord under EVERY field the location can appear
        // in, not just the single "map". A location is often reachable as several
        // field variants of the same room (disc-1/disc-2 versions, e.g.
        // subin_1a + subin_1b). Keying only "map" left the BITON in the OTHER
        // variants unmatched -> NEUTRALIZED, so grabbing the pickup in whichever
        // variant the player actually reached set a dead flag and the check never
        // fired (the Red Sub / Huge Materia Underwater bug). Each map is its own
        // queue and they all relocate to the SAME coord, so there is still exactly
        // one detection flag per location (no double-checks).
        QStringList fields;
        const QJsonArray mapsArr = p["maps"].toArray();
        for (const QJsonValue& m : mapsArr) {
            QString f = m.toString().toLower().trimmed();
            if (!f.isEmpty()) fields << f;
        }
        if (fields.isEmpty()) {
            QString f = p["map"].toString().toLower().trimmed();
            if (!f.isEmpty()) fields << f;
        }
        for (const QString& field : fields)
            m_apJsonLookup[field + QChar('|') + itemText].enqueue(coord);
    }

    debugStream << "AP JSON: loaded " << placements.size() << " placements ("
                << m_apJsonLookup.size() << " unique field|item keys) from "
                << path << "\n\n";
    return true;
}

// ============================================================================
// jsonBankToNibble  –  translate an Archipelago locations.json bank number
//                      into the field-script bank NIBBLE the engine resolves.
//
// Engine truth (ff7_en.exe var-resolve jump table @VA 0x60fa49 / index table
// 0x60fa6d): script nibble -> target:
//   1/2 -> savemap+0xBA4   3/4 -> +0xCA4   5/6 -> TEMP 0xCC14D0 (NOT savemap!)
//   B/C -> +0xDA4          D/E -> +0xEA4   7/F -> +0xFA4
//   8/9/A invalid; NO nibble reaches savemap+0x10A4.
//
// The JSON/client numbering (FF7Client.py _BANK_BASE) is linear:
//   1/2 -> 0xBA4, 3/4 -> 0xCA4, 5/6 -> 0xDA4, 11/12 -> 0xEA4,
//   13/14 -> 0xFA4, 15 -> 0x10A4.
//
// The old translation ((bank & 0x0F) << 4) only agrees for banks 1-4; every
// json bank >= 5 landed its BITON in the wrong savemap page (e.g. bank 13
// wrote to 0xEA4+addr instead of 0xFA4+addr), corrupting unrelated variable
// space while the client watched a byte that never changed. Returns -1 for
// banks no field-script nibble can reach (json bank 15 = savemap+0x10A4).
// ============================================================================

static int jsonBankToNibble(quint8 jsonBank)
{
    switch (jsonBank) {
        case 1:  return 0x1;  // savemap +0xBA4 (8-bit)
        case 2:  return 0x2;  // savemap +0xBA4 (16-bit)
        case 3:  return 0x3;  // savemap +0xCA4
        case 4:  return 0x4;
        case 5:  return 0xB;  // savemap +0xDA4 (nibbles 5/6 are TEMP, not savemap)
        case 6:  return 0xC;
        case 11: return 0xD;  // savemap +0xEA4
        case 12: return 0xE;
        case 13: return 0xF;  // savemap +0xFA4
        case 14: return 0x7;
        default: return -1;   // bank 15 (+0x10A4) unreachable from field script
    }
}

// ============================================================================
// applySTITMAsArchipelago  –  replace STITM(5B) with BITON(4B) + NOP(1B)
//                              using the pre-assigned address/bit from the
//                              Archipelago JSON.  Falls back with a warning
//                              if no JSON entry matches.
// ============================================================================

bool FieldPickupRandomizer_ff7tk::applySTITMAsArchipelago(
    STITMInfo& info,
    QByteArray& fieldData,
    const QString& fieldName,
    QTextStream& debugStream)
{
    if (info.offset + STITM_SIZE > fieldData.size()) return false;

    QString itemName = getItemName(info.originalItemID).toLower().trimmed();
    QString key      = fieldName.toLower().trimmed() + QChar('|') + itemName;

    ApBitonCoord biton;
    bool reusedBiton = false;
    if (m_apJsonLookup.contains(key) && !m_apJsonLookup[key].isEmpty()) {
        biton = m_apJsonLookup[key].dequeue();
        m_apJsonLastBiton[key] = biton;
    } else if (m_apJsonLastBiton.contains(key)) {
        // Duplicate STITM in the same field — reuse the last BITON so the
        // location is tracked regardless of which opcode the player triggers.
        biton = m_apJsonLastBiton[key];
        reusedBiton = true;
    } else {
        debugStream << "  AP_STITM @" << info.offset
                    << " WARN: no JSON entry for ("
                    << fieldName << ", " << getItemName(info.originalItemID)
                    << ") – location will not be tracked\n";
        return false;
    }
    quint8 destBank     = biton.bank;
    quint8 addr         = biton.address;
    quint8 bit          = biton.bit;
    // BITON encodes the dest bank NIBBLE in the high nibble of byte 1, src
    // bank in low. src = 0 always (we write a literal bit, not from another
    // var). The nibble is NOT the json bank number for banks >= 5.
    int nibble = jsonBankToNibble(destBank);
    if (nibble < 0) {
        debugStream << "  AP_STITM @" << info.offset
                    << " WARN: json bank " << destBank
                    << " has no field-script nibble – "
                    << getItemName(info.originalItemID)
                    << " location cannot be tracked, STITM left intact\n";
        return false;
    }
    quint8 bankByte     = static_cast<quint8>(nibble << 4);

    fieldData[info.offset]     = static_cast<char>(BITON_OPCODE);
    fieldData[info.offset + 1] = static_cast<char>(bankByte);
    fieldData[info.offset + 2] = static_cast<char>(addr);
    fieldData[info.offset + 3] = static_cast<char>(bit);
    fieldData[info.offset + 4] = static_cast<char>(0x5F); // NOP – pads former 5th STITM byte

    ApBitonEntry entry;
    entry.field          = fieldName;
    entry.offset         = info.offset;
    entry.isMateria      = false;
    entry.originalItemId = info.originalItemID;
    entry.originalName   = getItemName(info.originalItemID);
    entry.jsonBank       = destBank;
    entry.bankByte       = bankByte;
    entry.address        = addr;
    entry.bit            = bit;
    m_apBitonEntries.append(entry);

    debugStream << "  AP_STITM @" << info.offset
                << "  " << entry.originalName
                << " (" << info.originalItemID << ")"
                << " -> BITON bank=" << destBank
                << " addr=0x" << QString::number(addr, 16)
                << " bit=" << bit
                << (reusedBiton ? " (reused)" : "") << "\n";
    return true;
}

// ============================================================================
// applySMTRAAsArchipelago  –  replace SMTRA(7B) with BITON(4B) + NOP×3(3B)
//                              using the pre-assigned address/bit from JSON.
// ============================================================================

bool FieldPickupRandomizer_ff7tk::applySMTRAAsArchipelago(
    SMTRAInfo& info,
    QByteArray& fieldData,
    const QString& fieldName,
    QTextStream& debugStream)
{
    if (info.offset + SMTRA_SIZE > fieldData.size()) return false;

    QString materiaName = getMateriaName(info.originalMateriaID).toLower().trimmed();
    QString key         = fieldName.toLower().trimmed() + QChar('|') + materiaName;

    ApBitonCoord biton;
    bool reusedBiton = false;
    if (m_apJsonLookup.contains(key) && !m_apJsonLookup[key].isEmpty()) {
        biton = m_apJsonLookup[key].dequeue();
        m_apJsonLastBiton[key] = biton;
    } else if (m_apJsonLastBiton.contains(key)) {
        // Duplicate SMTRA in the same field — reuse the last BITON so the
        // location is tracked regardless of which opcode the player triggers.
        biton = m_apJsonLastBiton[key];
        reusedBiton = true;
    } else {
        debugStream << "  AP_SMTRA @" << info.offset
                    << " WARN: no JSON entry for ("
                    << fieldName << ", " << getMateriaName(info.originalMateriaID)
                    << ") – location will not be tracked\n";
        return false;
    }
    quint8 destBank     = biton.bank;
    quint8 addr         = biton.address;
    quint8 bit          = biton.bit;
    int nibble = jsonBankToNibble(destBank);
    if (nibble < 0) {
        debugStream << "  AP_SMTRA @" << info.offset
                    << " WARN: json bank " << destBank
                    << " has no field-script nibble – "
                    << getMateriaName(info.originalMateriaID)
                    << " location cannot be tracked, SMTRA left intact\n";
        return false;
    }
    quint8 bankByte     = static_cast<quint8>(nibble << 4);

    fieldData[info.offset]     = static_cast<char>(BITON_OPCODE);
    fieldData[info.offset + 1] = static_cast<char>(bankByte);
    fieldData[info.offset + 2] = static_cast<char>(addr);
    fieldData[info.offset + 3] = static_cast<char>(bit);
    fieldData[info.offset + 4] = static_cast<char>(0x5F); // NOP ×3 – pads former SMTRA bytes
    fieldData[info.offset + 5] = static_cast<char>(0x5F);
    fieldData[info.offset + 6] = static_cast<char>(0x5F);

    ApBitonEntry entry;
    entry.field            = fieldName;
    entry.offset           = info.offset;
    entry.isMateria        = true;
    entry.originalMateriaId = info.originalMateriaID;
    entry.originalName     = getMateriaName(info.originalMateriaID);
    entry.jsonBank         = destBank;
    entry.bankByte         = bankByte;
    entry.address          = addr;
    entry.bit              = bit;
    m_apBitonEntries.append(entry);

    debugStream << "  AP_SMTRA @" << info.offset
                << "  " << entry.originalName
                << " (" << info.originalMateriaID << ")"
                << " -> BITON bank=" << destBank
                << " addr=0x" << QString::number(addr, 16)
                << " bit=" << bit
                << (reusedBiton ? " (reused)" : "") << "\n";
    return true;
}

// ============================================================================
// replaceVanillaBitonsForAP  –  replace vanilla key-item BITONs with AP BITONs
//
// In AP mode, vanilla key items use pre-existing BITON opcodes (not STITM).
// This function scans for those BITONs and replaces them with AP-allocated
// BITONs from the JSON lookup, enabling AP tracking for key item locations.
//
// Wardrobe categories (Dress, Wig, Tiara, Cologne, Underwear, Medicine) share
// the same AP BITON within each category, matching vanilla behavior.
// ============================================================================

// Helper: Get wardrobe category from item name (returns int matching WardrobeCategory enum)
static int getWardrobeCategoryFromName(const QString& itemName)
{
    QString lower = itemName.toLower();
    if (lower.contains("dress")) return 1;  // Dress
    if (lower == "wig" || lower == "dyed wig" || lower == "blonde wig") return 2;  // Wig
    if (lower.contains("tiara")) return 3;  // Tiara
    if (lower.contains("cologne") || lower == "pharmacy coupon") return 4;  // Cologne
    if (lower == "lingerie" || lower == "mystery panties" || lower == "bikini briefs") return 5;  // Underwear
    if (lower == "disinfectant" || lower == "deodorant" || lower == "digestive") return 6;  // Medicine
    return 0;  // None
}

// Helper: Get representative item name for wardrobe category lookup
static QString getCategoryItemName(const QString& itemName)
{
    QString lower = itemName.toLower();
    if (lower.contains("dress")) return "Cotton Dress";
    if (lower == "wig" || lower == "dyed wig" || lower == "blonde wig") return "Wig";
    if (lower.contains("tiara")) return "Glass Tiara";
    if (lower.contains("cologne")) return "Cologne";
    if (lower == "pharmacy coupon") return "Cologne";  // Same category as cologne
    if (lower == "lingerie" || lower == "mystery panties" || lower == "bikini briefs") return "Lingerie";
    if (lower == "disinfectant" || lower == "deodorant" || lower == "digestive") return "Disinfectant";
    return itemName;
}

int FieldPickupRandomizer_ff7tk::replaceVanillaBitonsForAP(
    QByteArray& decompressed,
    const QString& fieldName,
    QTextStream& debugStream)
{
    int modified = 0;
    const int fileSize = decompressed.size();

    // Need at least the 42-byte header (6 + 9*4)
    const int HEADER_SIZE = 6 + 9 * 4;
    if (fileSize < HEADER_SIZE) return 0;

    // Parse section table
    quint32 sectionPositions[9];
    memcpy(sectionPositions, decompressed.constData() + 6, 9 * 4);
    quint32 sec0off = sectionPositions[0];
    if (sec0off + 4 >= static_cast<quint32>(fileSize)) return 0;

    int sec0DataStart = static_cast<int>(sec0off) + 4;
    quint8 nbEnt = static_cast<quint8>(decompressed.at(sec0DataStart + 2));
    int scriptStart = sec0DataStart + 32 + 72 * nbEnt;

    quint16 posTexts;
    memcpy(&posTexts, decompressed.constData() + sec0DataStart + 4, 2);
    int scriptEnd = sec0DataStart + posTexts;

    if (scriptStart >= scriptEnd || scriptEnd > fileSize) return 0;

    // Track assigned BITONs per field per wardrobe category (for sharing)
    QMap<QString, ApBitonCoord> categoryBitons;
    // Track the AP flag assigned to each (field|keyitem) so SIBLING BITONs of the
    // same key item (the field grants it via several code paths, but there is only
    // one AP location entry) reuse it. Without this the un-matched copies are left
    // intact, so the in-game pickup still sets the real key-item bit and grants
    // that item's access on pickup (the "checking the location grants access" bug).
    QMap<QString, ApBitonCoord> keyItemBitons;

    // Scan script section for BITON opcodes
    for (int i = scriptStart; i < scriptEnd - 4; ++i) {
        quint8 opcode = static_cast<quint8>(decompressed.at(i));

        if (opcode == BITON_OPCODE && i + 3 < scriptEnd) {
            quint8 bankByte = static_cast<quint8>(decompressed.at(i + 1));
            quint8 addr = static_cast<quint8>(decompressed.at(i + 2));
            quint8 bit = static_cast<quint8>(decompressed.at(i + 3));

            quint8 destBank = (bankByte >> 4) & 0x0F;

            // Key item BITONs are in bank 1-2, addresses 0x40-0x46 (vanilla range)
            if (destBank >= 1 && destBank <= 2 && addr >= 0x40 && addr <= 0x46) {
                quint16 saveOffset = 0x0BA4 + addr;
                QString keyItemName = getKeyItemName(saveOffset, bit);

                if (!keyItemName.isEmpty() && !keyItemName.startsWith("KeyItem@")) {
                    // Check if this is a wardrobe category item
                    int categoryInt = getWardrobeCategoryFromName(keyItemName);
                    bool isWardrobe = (categoryInt != 0);
                    WardrobeCategory category = static_cast<WardrobeCategory>(categoryInt);
                    
                    // For wardrobe items, use the category representative name for lookup
                    QString lookupItemName = isWardrobe ? getCategoryItemName(keyItemName) : keyItemName;
                    QString key = fieldName.toLower().trimmed() + QChar('|') + lookupItemName.toLower().trimmed();
                    
                    // For wardrobe items, check if we already assigned a BITON for this category in this field
                    QString categoryKey = fieldName.toLower() + "|" + QString::number(static_cast<int>(category));
                    ApBitonCoord apBiton;
                    bool foundBiton = false;
                    bool reusedBiton = false;
                    
                    if (isWardrobe && categoryBitons.contains(categoryKey)) {
                        // Reuse existing BITON for this category
                        apBiton = categoryBitons[categoryKey];
                        foundBiton = true;
                        reusedBiton = true;
                    } else if (m_apJsonLookup.contains(key) && !m_apJsonLookup[key].isEmpty()) {
                        // Get new BITON from queue
                        apBiton = m_apJsonLookup[key].dequeue();
                        foundBiton = true;
                        if (isWardrobe) {
                            // Cache it for sharing within this category
                            categoryBitons[categoryKey] = apBiton;
                        }
                        keyItemBitons[key] = apBiton;  // cache for sibling BITONs
                    } else if (keyItemBitons.contains(key)) {
                        // Sibling BITON of the same key item (another code path):
                        // reuse its AP flag so EVERY path fires the check and NONE
                        // sets the real key-item bit -> no access granted on pickup.
                        apBiton = keyItemBitons[key];
                        foundBiton = true;
                        reusedBiton = true;
                    }

                    // A coord whose json bank no field-script nibble can reach
                    // (bank 15) is unwritable: fall through to NEUTRALIZE.
                    int nibble = foundBiton ? jsonBankToNibble(apBiton.bank) : -1;
                    if (foundBiton && nibble < 0) {
                        debugStream << "  AP_VANILLA_BITON @" << i
                                    << " WARN: json bank " << apBiton.bank
                                    << " has no field-script nibble ("
                                    << keyItemName << ")\n";
                        foundBiton = false;
                    }

                    if (foundBiton) {
                        quint8 newBankByte = static_cast<quint8>(nibble << 4);
                        decompressed[i + 1] = static_cast<char>(newBankByte);
                        decompressed[i + 2] = static_cast<char>(apBiton.address);
                        decompressed[i + 3] = static_cast<char>(apBiton.bit);

                        debugStream << "  AP_VANILLA_BITON @" << i
                                    << " " << keyItemName
                                    << " -> bank=" << apBiton.bank
                                    << " addr=0x" << QString::number(apBiton.address, 16)
                                    << " bit=" << apBiton.bit;
                        if (reusedBiton) {
                            debugStream << (isWardrobe
                                ? QString(" (shared %1)").arg(wardrobeCategoryName(category))
                                : QString(" (shared sibling)"));
                        }
                        debugStream << "\n";

                        ApBitonEntry entry;
                        entry.field = fieldName;
                        entry.offset = i;
                        entry.isMateria = false;
                        entry.originalItemId = 0;
                        entry.originalName = keyItemName;
                        entry.jsonBank = apBiton.bank;
                        entry.bankByte = newBankByte;
                        entry.address = apBiton.address;
                        entry.bit = apBiton.bit;
                        m_apBitonEntries.append(entry);

                        modified++;
                    } else {
                        // No AP entry and no sibling to share: NEUTRALIZE so the
                        // in-game pickup can't grant the real key item (in AP /
                        // Free Roam every key item comes from Archipelago). Redirect
                        // the BITON to the unused key-item byte 0xFE (a harmless
                        // flag), exactly as the non-AP keyItemMod path does. The
                        // location won't be tracked, but the access leak is gone.
                        decompressed[i + 2] = static_cast<char>(0xFE);
                        debugStream << "  AP_VANILLA_BITON @" << i
                                    << " NEUTRALIZED (no AP entry for " << keyItemName
                                    << ") -> addr 0xFE\n";
                        modified++;
                    }
                }
            }
        }
    }

    return modified;
}

// ============================================================================
// writeArchipelagoSidecar  –  emit archipelago_bitons.json
//
// Format:
//   {
//     "biton_map": [
//       {
//         "field": "mds7st1",
//         "offset": 2908,
//         "is_materia": false,
//         "original_item_id": 32,
//         "original_name": "Hi-Potion",
//         "bank": 1,
//         "address": 128,
//         "bit": 0
//       }, ...
//     ]
//   }
//
// The Archipelago side matches entries by (field, original_name) to
// location codes and updates locations.json via:
//   python tools/map_biton_flags.py --ap-sidecar archipelago_bitons.json
// ============================================================================

void FieldPickupRandomizer_ff7tk::writeArchipelagoSidecar(
    const QString& outputPath,
    QTextStream& debugStream) const
{
    QString sidecarPath = outputPath + "/archipelago_bitons.json";

    QJsonArray arr;
    for (const ApBitonEntry& e : m_apBitonEntries) {
        QJsonObject obj;
        obj["field"]          = e.field;
        obj["offset"]         = e.offset;
        obj["is_materia"]     = e.isMateria;
        obj["original_item_id"] = e.isMateria
                                    ? static_cast<int>(e.originalMateriaId)
                                    : static_cast<int>(e.originalItemId);
        obj["original_name"]  = e.originalName;
        // Report the JSON bank number, NOT the script nibble in bankByte —
        // they differ for banks >= 5 and the AP side speaks json numbering.
        obj["bank"]           = static_cast<int>(e.jsonBank);
        obj["address"]        = static_cast<int>(e.address);
        obj["bit"]            = static_cast<int>(e.bit);
        arr.append(obj);
    }

    QJsonObject root;
    root["biton_map"] = arr;

    QFile f(sidecarPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson());
        f.close();
        debugStream << "\nArchipelago sidecar written: " << sidecarPath
                    << "  (" << m_apBitonEntries.size() << " entries)\n";
        qDebug() << "Archipelago sidecar written:" << sidecarPath;
    } else {
        debugStream << "\nERROR: could not write Archipelago sidecar: " << sidecarPath << "\n";
        qDebug() << "ERROR writing Archipelago sidecar:" << f.errorString();
    }
}

// ============================================================================
// updateFieldTexts  –  parse text section in section 0, replace item/materia
//                      names, rebuild text section with correct offsets.
//
// Section 0 layout within decompressed field data:
//   sectionPositions[0]     = offset of section 0 size header (4 bytes)
//   sectionPositions[0]+4   = start of section 0 data
//   sectionPositions[1]     = start of section 1
//
// Section 0 internal layout (from Makou Section1File):
//   offset 0-1 : version
//   offset 2   : nbScripts
//   offset 4-5 : posTexts (relative to section 0 data start)
//   offset 6-7 : nbAKAO
//   ...header, script names, AKAO positions, script positions, scripts...
//   posTexts   : text section start
//   posAKAO    : AKAO data start
//
// Text section format:
//   [2 bytes: textCount]
//   [textCount * 2 bytes: offset table (each relative to posTexts)]
//   [text data: encoded strings separated by 0xFF terminators]
// ============================================================================

bool FieldPickupRandomizer_ff7tk::updateFieldTexts(
    QByteArray& decompressed,
    const QVector<OpcodeModification>& modifications,
    QTextStream& debugStream)
{
    if (modifications.isEmpty()) return false;

    const int fileSize = decompressed.size();
    const int FIELD_HEADER_SIZE = 6 + 9 * 4; // 42 bytes
    if (fileSize < FIELD_HEADER_SIZE) return false;

    // --- parse field section positions --------------------------------------
    quint32 sectionPositions[9];
    memcpy(sectionPositions, decompressed.constData() + 6, 9 * 4);

    quint32 sec0off = sectionPositions[0];
    quint32 sec1off = sectionPositions[1];
    if (sec0off + 4 >= static_cast<quint32>(fileSize) ||
        sec1off > static_cast<quint32>(fileSize) ||
        sec1off <= sec0off + 4)
        return false;

    int sec0DataStart = static_cast<int>(sec0off) + 4; // skip size header
    int sec0DataLen   = static_cast<int>(sec1off) - sec0DataStart;
    if (sec0DataLen < 32) return false;

    // --- parse section 0 header ---------------------------------------------
    quint16 posTexts;
    memcpy(&posTexts, decompressed.constData() + sec0DataStart + 4, 2);
    if (posTexts + 4 > sec0DataLen) return false;

    quint8  nbScripts = static_cast<quint8>(decompressed.at(sec0DataStart + 2));
    quint16 nbAKAO;
    memcpy(&nbAKAO, decompressed.constData() + sec0DataStart + 6, 2);

    // Script section absolute bounds (for MESSAGE search)
    int scriptAbsEnd = sec0DataStart + posTexts;

    // Determine where AKAO data starts (end of text section)
    quint32 posAKAO;
    if (nbAKAO > 0) {
        int akaoTableOff = 32 + 8 * nbScripts;
        if (akaoTableOff + 4 > sec0DataLen) return false;
        memcpy(&posAKAO, decompressed.constData() + sec0DataStart + akaoTableOff, 4);
    } else {
        posAKAO = static_cast<quint32>(sec0DataLen);
    }
    if (posTexts >= posAKAO || posAKAO > static_cast<quint32>(sec0DataLen))
        return false;

    // --- parse existing text entries ----------------------------------------
    quint16 firstTextOff;
    memcpy(&firstTextOff, decompressed.constData() + sec0DataStart + posTexts + 2, 2);
    if (firstTextOff < 4) return false;

    int textCount = firstTextOff / 2 - 1;
    if (textCount <= 0 || textCount > 255) return false;

    // Read offset table
    QVector<quint16> offsets(textCount);
    for (int i = 0; i < textCount; ++i)
        memcpy(&offsets[i], decompressed.constData() + sec0DataStart + posTexts + 2 + i * 2, 2);

    // Extract each text entry (raw FF7-encoded bytes, without 0xFF terminator)
    QVector<QByteArray> textEntries;
    for (int i = 0; i < textCount; ++i) {
        int start = sec0DataStart + posTexts + offsets[i];
        int end   = (i + 1 < textCount)
                        ? sec0DataStart + posTexts + offsets[i + 1]
                        : sec0DataStart + static_cast<int>(posAKAO);
        if (start >= fileSize || end > fileSize || end <= start) {
            textEntries.append(QByteArray());
            continue;
        }
        QByteArray entry(decompressed.constData() + start, end - start);
        while (!entry.isEmpty() && static_cast<quint8>(entry.back()) == 0xFF)
            entry.chop(1);
        textEntries.append(entry);
    }

    // --- for each modification, find nearby MESSAGE and create new text -----
    // In FF7 chest scripts, MESSAGE typically precedes STITM:
    //   MESSAGE windowID textID  (show "Received X!")
    //   ...
    //   STITM itemID qty         (give item)
    // So we search BOTH directions, pick the closest, and track used offsets
    // to prevent multiple STITMs from claiming the same MESSAGE.
    bool anyChanged = false;
    QVector<QByteArray> newTextEntries;       // new texts to append
    QVector<QPair<int, int>> messagePatches;  // (absOffset of MESSAGE textID byte, newTextID)
    QSet<int> usedMessageOffsets;             // prevent double-assignment

    for (const auto& mod : modifications) {
        int backOff = -1, fwdOff = -1;

        // Search backward first (up to 500 bytes)
        {
            int searchStart = qMax(mod.opcodeOffset - 500, sec0DataStart);
            for (int pos = mod.opcodeOffset - 1; pos >= searchStart; --pos) {
                if (static_cast<quint8>(decompressed.at(pos)) == MESSAGE_OPCODE
                    && pos + 2 < scriptAbsEnd) {
                    quint8 winID = static_cast<quint8>(decompressed.at(pos + 1));
                    quint8 txtID = static_cast<quint8>(decompressed.at(pos + 2));
                    if (winID <= 15 && txtID < textCount && !usedMessageOffsets.contains(pos)) {
                        backOff = pos;
                        break;
                    }
                }
            }
        }

        // Search forward (up to 500 bytes), but skip past the BITON/STITM bytes
        // we just wrote. The placement's address byte can equal MESSAGE_OPCODE
        // (e.g. wardrobe key items use address 0x40 == MESSAGE), which would
        // otherwise produce a false-positive MESSAGE hit and clobber the byte
        // immediately after our placement when its textID is patched.
        {
            const int skipPlacementBytes = 5; // covers 4-byte BITON or 5-byte STITM slot
            int searchEnd = qMin(mod.opcodeOffset + 500, scriptAbsEnd - 2);
            for (int pos = mod.opcodeOffset + skipPlacementBytes; pos < searchEnd; ++pos) {
                if (static_cast<quint8>(decompressed.at(pos)) == MESSAGE_OPCODE) {
                    quint8 winID = static_cast<quint8>(decompressed.at(pos + 1));
                    quint8 txtID = static_cast<quint8>(decompressed.at(pos + 2));
                    if (winID <= 15 && txtID < textCount && !usedMessageOffsets.contains(pos)) {
                        fwdOff = pos;
                        break;
                    }
                }
            }
        }

        // Pick the closest MESSAGE
        int messageOff = -1;
        if (backOff >= 0 && fwdOff >= 0) {
            int backDist = mod.opcodeOffset - backOff;
            int fwdDist  = fwdOff - mod.opcodeOffset;
            messageOff = (backDist <= fwdDist) ? backOff : fwdOff;
        } else if (backOff >= 0) {
            messageOff = backOff;
        } else if (fwdOff >= 0) {
            messageOff = fwdOff;
        }

        if (messageOff < 0) {
            debugStream << "  No MESSAGE near @" << mod.opcodeOffset << "\n";
            continue;
        }

        // Build new text string
        QString newTextStr;
        if (mod.isMateria)
            newTextStr = QStringLiteral("Received \"%1\" Materia!").arg(mod.newName);
        else
            newTextStr = QStringLiteral("Received \"%1\"!").arg(mod.newName);

        QByteArray newTextData = FF7Text::toFF7(newTextStr);

        int newTextID = textCount + newTextEntries.size();
        if (newTextID > 255) {
            debugStream << "  Text table full (>255), skipping @" << mod.opcodeOffset << "\n";
            continue;
        }

        newTextEntries.append(newTextData);
        messagePatches.append({messageOff + 2, newTextID}); // +2 = textID byte offset
        usedMessageOffsets.insert(messageOff);
        anyChanged = true;

        debugStream << "  MSG @" << messageOff << " textID "
                    << static_cast<int>(static_cast<quint8>(decompressed.at(messageOff + 2)))
                    << " -> " << newTextID << "  " << newTextStr << "\n";
    }

    if (!anyChanged) return false;

    // --- patch MESSAGE textID bytes in the decompressed data ----------------
    // (scripts are BEFORE the text section, so their offsets won't shift)
    for (const auto& patch : messagePatches)
        decompressed[patch.first] = static_cast<char>(patch.second);

    // --- rebuild text section with original + new entries --------------------
    int totalTexts = textCount + newTextEntries.size();
    QByteArray newTextSection;
    quint16 tc = static_cast<quint16>(totalTexts);
    newTextSection.append(reinterpret_cast<const char*>(&tc), 2);

    int offsetTableSize = 2 + totalTexts * 2;

    QByteArray textData;
    QVector<quint16> newOffsets;

    // Original text entries
    for (int i = 0; i < textCount; ++i) {
        quint16 off = static_cast<quint16>(offsetTableSize + textData.size());
        newOffsets.append(off);
        textData.append(textEntries[i]);
        textData.append('\xFF');
    }
    // New text entries
    for (const auto& nt : newTextEntries) {
        quint16 off = static_cast<quint16>(offsetTableSize + textData.size());
        newOffsets.append(off);
        textData.append(nt);
        textData.append('\xFF');
    }

    // Write offset table
    for (int i = 0; i < totalTexts; ++i)
        newTextSection.append(reinterpret_cast<const char*>(&newOffsets[i]), 2);

    newTextSection.append(textData);

    // --- compute size delta and rebuild decompressed data --------------------
    int oldTextSectionSize = static_cast<int>(posAKAO) - posTexts;
    int newTextSectionSize = newTextSection.size();
    int delta = newTextSectionSize - oldTextSectionSize;

    int textAbsStart = sec0DataStart + posTexts;
    int akaoAbsStart = sec0DataStart + static_cast<int>(posAKAO);

    QByteArray result;
    result.append(decompressed.left(textAbsStart));   // header + scripts (with patched MESSAGE IDs)
    result.append(newTextSection);                     // rebuilt text section
    result.append(decompressed.mid(akaoAbsStart));     // AKAO + remaining sections

    // Update section 0 size header
    quint32 oldSec0Size;
    memcpy(&oldSec0Size, result.constData() + sec0off, 4);
    quint32 newSec0Size = static_cast<quint32>(static_cast<int>(oldSec0Size) + delta);
    memcpy(result.data() + sec0off, &newSec0Size, 4);

    // Update AKAO position table entries (shift by delta)
    if (nbAKAO > 0 && delta != 0) {
        int akaoTableOff = sec0DataStart + 32 + 8 * nbScripts;
        for (int i = 0; i < nbAKAO; ++i) {
            quint32 pos;
            memcpy(&pos, result.constData() + akaoTableOff + i * 4, 4);
            pos = static_cast<quint32>(static_cast<int>(pos) + delta);
            memcpy(result.data() + akaoTableOff + i * 4, &pos, 4);
        }
    }

    // Update section positions 1-8 in field header (shift by delta)
    if (delta != 0) {
        for (int s = 1; s < 9; ++s) {
            quint32 pos;
            memcpy(&pos, result.constData() + 6 + s * 4, 4);
            pos = static_cast<quint32>(static_cast<int>(pos) + delta);
            memcpy(result.data() + 6 + s * 4, &pos, 4);
        }
    }

    decompressed = result;

    debugStream << "  Texts: " << textCount << " + " << newTextEntries.size()
                << " new = " << totalTexts << "  (delta=" << delta << " bytes)\n";

    return true;
}

// ============================================================================
// Key item randomization – progression sphere system
// ============================================================================

// Key item IDs (address << 8 | bit) – matches BITON opcode encoding
static const quint32 KEY_COTTON_DRESS = 0x4000;
static const quint32 KEY_SATIN_DRESS  = 0x4001;
static const quint32 KEY_SILK_DRESS   = 0x4002;
static const quint32 KEY_WIG          = 0x4003;
static const quint32 KEY_DYED_WIG     = 0x4004;
static const quint32 KEY_BLONDE_WIG   = 0x4005;
static const quint32 KEY_GLASS_TIARA  = 0x4006;
static const quint32 KEY_RUBY_TIARA   = 0x4007;
static const quint32 KEY_DIAMOND_TIARA    = 0x4100;
static const quint32 KEY_COLOGNE          = 0x4101;
static const quint32 KEY_FLOWER_COLOGNE   = 0x4102;
static const quint32 KEY_SEXY_COLOGNE     = 0x4103;
static const quint32 KEY_MEMBERS_CARD     = 0x4104;
static const quint32 KEY_LINGERIE         = 0x4105;
static const quint32 KEY_MYSTERY_PANTIES  = 0x4106;
static const quint32 KEY_BIKINI_BRIEFS    = 0x4107;
static const quint32 KEY_PHARMACY_COUPON  = 0x4200;
static const quint32 KEY_DISINFECTANT     = 0x4201;
static const quint32 KEY_DEODORANT        = 0x4202;
static const quint32 KEY_DIGESTIVE        = 0x4203;
static const quint32 KEY_HUGE_MATERIA_FC        = 0x4204;
static const quint32 KEY_HUGE_MATERIA_COREL     = 0x4205;
static const quint32 KEY_HUGE_MATERIA_UNDERWATER= 0x4206;
static const quint32 KEY_HUGE_MATERIA_ROCKET    = 0x4207;
static const quint32 KEY_KEY_TO_ANCIENTS  = 0x4300;
static const quint32 KEY_LETTER_TO_WIFE   = 0x4301;
static const quint32 KEY_LETTER_TO_DAUGHTER = 0x4302;
static const quint32 KEY_LUNAR_HARP       = 0x4303;
static const quint32 KEY_BASEMENT_KEY     = 0x4304;
static const quint32 KEY_KEY_TO_SECTOR_5  = 0x4305;
static const quint32 KEY_KEYCARD_60       = 0x4306;
static const quint32 KEY_KEYCARD_62       = 0x4307;
static const quint32 KEY_KEYCARD_65       = 0x4400;
static const quint32 KEY_KEYCARD_66       = 0x4401;
static const quint32 KEY_KEYCARD_68       = 0x4402;
static const quint32 KEY_MIDGAR_PARTS_1   = 0x4403;
static const quint32 KEY_MIDGAR_PARTS_2   = 0x4404;
static const quint32 KEY_MIDGAR_PARTS_3   = 0x4405;
static const quint32 KEY_MIDGAR_PARTS_4   = 0x4406;
static const quint32 KEY_MIDGAR_PARTS_5   = 0x4407;
static const quint32 KEY_PHS              = 0x4500;
static const quint32 KEY_GOLD_TICKET      = 0x4501;
static const quint32 KEY_KEYSTONE         = 0x4502;
static const quint32 KEY_LEVIATHAN_SCALES = 0x4503;
static const quint32 KEY_GLACIER_MAP      = 0x4504;
static const quint32 KEY_A_COUPON         = 0x4505;
static const quint32 KEY_B_COUPON         = 0x4506;
static const quint32 KEY_C_COUPON         = 0x4507;
static const quint32 KEY_BLACK_MATERIA    = 0x4600;
static const quint32 KEY_MYTHRIL          = 0x4601;
static const quint32 KEY_SNOWBOARD        = 0x4602;

int FieldPickupRandomizer_ff7tk::getFieldSphere(const QString& fieldName)
{
    static const QSet<QString> sphere0 = {
        "mds7st3","mds7_w1","mds7_w2","mds7_w3",
        "md1stin","md1_1","md1_2","nmkin_1","nmkin_2","nmkin_3","nmkin_4","nmkin_5",
        "nrthmk","southmk1","southmk2",
        "md8_1","md8_2","md8_3","md8_4","md8brdg1","md8brdg2",
        "mds7plr1","mds7plr2","tin_1","tin_2","tin_3","tin_4",
        "7min1","7min2","7min3","sector1","sector2"
    };
    static const QSet<QString> sphere1 = {
        "mkt_s1","mkt_s2","mkt_s3","mkt_w","mkt_mens",
        "mkt_m","mkt_pub","mktpb","mkt_inn",
        "onna_1","onna_2","onna_3","onna_4","onna_5","onna_51","onna_52",
        "mds5_1","mds5_2","mds5_3","mds5_4","mds5_5","church","church2"
    };
    static const QSet<QString> sphere2 = {
        "colne_1","colne_2","colne_3","colne_4","colne_5","colne_6",
        "mds7st1","mds7st2",
    };
    static const QSet<QString> sphere3 = { "blin1","blin2_1","blin2_2","blin2_3","blin59" };
    static const QSet<QString> sphere4 = {
        "blin60","blin61","blin62_1","blin62_2","blin63_1","blin63_2","blin64"
    };
    static const QSet<QString> sphere5 = { "blin65_1","blin65_2" };
    static const QSet<QString> sphere6 = {
        "blin66_1","blin66_2","blin66_3","blin66_4","blin66_5","blin66_6"
    };
    static const QSet<QString> sphere7 = {
        "blin67_1","blin67_2","blin67_3","blin67_4","blin671b","blin673b",
        "blin68_1","blin68_2","blin69_1",
        "blin70_1","blin70_2","blin70_3","blin70_4",
        "blinst_1","blinst_2","blinst_3","blinele"
    };
    static const QSet<QString> sphere8 = {
        "elmin1_1","elmin1_2","elmin2_1","elmin2_2","elmin3_1","elmin3_2",
        "elminn_1","elminn_2","farm","frcyo","frcyo_2",
        "junin1","junin2","junin3","junin4","junin5","junin6","junin7",
        "junone1","junone2","junone3","junone4","junone5","junone6",
        "jurone1","jurone2","jurone3","jurone4","jurone5","junpb_1","junpb_2",
        "ujunon1","ujunon2","ujunon3","junmin1","junmin2",
        "junonr1","junonr2","junonr3","junonr4",
        "jetin1","jetin2","jetin3",
        "condor1","condor2","convil_1","convil_2","convil_3","convil_4", "delmin12",
        "corel1","corel2","corel3","corelin",
        "ncorel1","ncorel2","ncorel3","ncorel4","ncoin1","ncoin2","ncoin3",
        "mtcrl_1","mtcrl_2","mtcrl_3","mtcrl_4","mtcrl_5","mtcrl_6","mtcrl_7","mtcrl_8","mtcrl_9",
        "ropest","ropein","games_1","games_2","ggate_1","ggate_2","ggate_3",
        "chorace","chorace2","coloin1","coloin2",
        "clsin2_1","clsin2_2","clsin2_3","desert1","desert2","coloss"
    };
    static const QSet<QString> sphere9 = {
        "cosmo","cosmo2","cosin1","cosin1_1","cosin2","cosin3","cosin4","cosin5",
        "cosmin2","cosmin3","cosmin4","cosmin6","cosmin7","cos_btm","cos_btm2",
        "gidun_1","gidun_2","gidun_3","gidun_4",
        "nivl_1","nivl_2","nivl_3","nivl_4","nivl_e",
        "niv_w","niv_ti1","niv_ti2",
        "sinin1_1","sinin1_2","sinin2_1","sinin2_2","sinin2_3","sinin3_1","sinin3_2",
        "sinbil_1","sinbil_2","sninn_1","sninn_2","sninn_3"
    };
    static const QSet<QString> sphere10 = {
        "rckt","rckt2","rckt3","rcktin1","rcktin2","rcktin3","rcktin4","rcktin5","rcktin6",
        "rktmin1","rktmin2","rkt_i",
        "utai_1","utai_2","utai_3","utai_4","utai_5",
        "utapb","utmin1","utmin2","utmin3","uttmpin1","uttmpin2","uttmpin3",
        "kuro_1","kuro_2","kuro_3","kuro_4","kuro_5","kuro_6","kuro_7","kuro_8",
        "yougan","yougan2"
    };
    static const QSet<QString> sphere11 = {
        "trnad_1","trnad_2","trnad_3","trnad_4",
    };
    static const QSet<QString> sphere12 = {
        "bonevil","bonevil2","slfrst_1","slfrst_2","slfrst_3"
    };
    static const QSet<QString> sphere13 = {
        "ancnt1","ancnt2","ancnt3","ancnt4",
        "anfrst_1","anfrst_2","anfrst_3","anfrst_4","anfrst_5",
        "losin1","losin2","losin3","losinn", "mkt_ia"
    };
    static const QSet<QString> sphere14 = {
        "hyou1","hyou2","hyou3","hyou4","hyou5_1","hyou5_2","hyou5_3","hyou5_4",
        "hyou6","hyou7","hyou8_1","hyou8_2","hyou9","hyou10","hyou11","hyou12","hyou13",
        "icedun_1","icedun_2","icedun_3","icedun_4",
        "snmin1","snmin2","snmin3",
        "gaiin_1","gaiin_2","gaiin_3","gaiin_4","gaiin_5","gaiin_6",
        "psdun_1","psdun_2","psdun_3","psdun_4"
    };
    static const QSet<QString> sphere15 = {
        "crater_1","crater_2","crater_3","crater_4",
        "las0_1","las0_2","las0_3","las0_4","las0_5","las0_6","las0_7",
        "las1_1","las1_2","las1_3","las1_4","las2_1","las2_2","las2_3",
        "las3_1","las3_2","las3_3","las4_0","las4_1","las4_2","las4_3","las4_4",
        "lastmap","lastcin"
    };

    QString name = fieldName.toLower();
    if (sphere0.contains(name))  return 0;
    if (sphere1.contains(name))  return 1;
    if (sphere2.contains(name))  return 2;
    if (sphere3.contains(name))  return 3;
    if (sphere4.contains(name))  return 4;
    if (sphere5.contains(name))  return 5;
    if (sphere6.contains(name))  return 6;
    if (sphere7.contains(name))  return 7;
    if (sphere8.contains(name))  return 8;
    if (sphere9.contains(name))  return 9;
    if (sphere10.contains(name)) return 10;
    if (sphere11.contains(name)) return 11;
    if (sphere12.contains(name)) return 12;
    if (sphere13.contains(name)) return 13;
    if (sphere14.contains(name)) return 14;
    if (sphere15.contains(name)) return 15;
    return 99;
}

int FieldPickupRandomizer_ff7tk::getKeyItemMinSphere(quint32 keyItemId)
{
    switch (keyItemId) {
    case KEY_COTTON_DRESS: case KEY_SATIN_DRESS: case KEY_SILK_DRESS:
    case KEY_WIG: case KEY_DYED_WIG: case KEY_BLONDE_WIG:
    case KEY_GLASS_TIARA: case KEY_RUBY_TIARA: case KEY_DIAMOND_TIARA:
    case KEY_COLOGNE: case KEY_FLOWER_COLOGNE: case KEY_SEXY_COLOGNE:
    case KEY_MEMBERS_CARD: case KEY_LINGERIE: case KEY_MYSTERY_PANTIES:
    case KEY_BIKINI_BRIEFS: case KEY_PHARMACY_COUPON: case KEY_DISINFECTANT:
    case KEY_DEODORANT: case KEY_DIGESTIVE:
    case KEY_KEYCARD_60: case KEY_PHS:
    case KEY_MIDGAR_PARTS_1: case KEY_MIDGAR_PARTS_2: case KEY_MIDGAR_PARTS_3:
    case KEY_MIDGAR_PARTS_4: case KEY_MIDGAR_PARTS_5:
    case KEY_KEYCARD_62: case KEY_KEYCARD_65: case KEY_KEYCARD_66: case KEY_KEYCARD_68: 
    case KEY_GOLD_TICKET: case KEY_KEYSTONE: case KEY_LUNAR_HARP:
    case KEY_SNOWBOARD: case KEY_BLACK_MATERIA: 
    case KEY_KEY_TO_ANCIENTS: case KEY_A_COUPON: case KEY_B_COUPON: case KEY_C_COUPON:
        return 0; // Available from start
    default: return 99;
    }
}

int FieldPickupRandomizer_ff7tk::getKeyItemMaxSphere(quint32 keyItemId)
{
    switch (keyItemId) {
    case KEY_COTTON_DRESS: case KEY_SATIN_DRESS: case KEY_SILK_DRESS:
    case KEY_WIG: case KEY_DYED_WIG: case KEY_BLONDE_WIG:
    case KEY_GLASS_TIARA: case KEY_RUBY_TIARA: case KEY_DIAMOND_TIARA:
    case KEY_COLOGNE: case KEY_FLOWER_COLOGNE: case KEY_SEXY_COLOGNE:
    case KEY_MEMBERS_CARD: case KEY_LINGERIE: case KEY_MYSTERY_PANTIES:
    case KEY_BIKINI_BRIEFS: 
    case KEY_DEODORANT: 
        return 1;
    case KEY_DIGESTIVE: case KEY_PHARMACY_COUPON: case KEY_DISINFECTANT:
    case KEY_KEYCARD_60:  return 3;
    case KEY_KEYCARD_62: case KEY_KEYCARD_65:
    case KEY_MIDGAR_PARTS_1: case KEY_MIDGAR_PARTS_2: case KEY_MIDGAR_PARTS_3:
    case KEY_MIDGAR_PARTS_4: case KEY_MIDGAR_PARTS_5:
        return 4;
    case KEY_KEYCARD_66:  return 6;
    case KEY_KEYCARD_68:  return 7;
    case KEY_KEYSTONE:    return 10;
    case KEY_LUNAR_HARP:  return 11;
    case KEY_SNOWBOARD:   return 13;
    case KEY_A_COUPON: case KEY_B_COUPON: case KEY_C_COUPON:
        return 6;
    default: return 99;
    }
}

int FieldPickupRandomizer_ff7tk::getKeyItemMinMoment(quint32 keyItemId)
{
    switch (keyItemId) {
    case KEY_GOLD_TICKET:
    case KEY_KEYSTONE:
    case KEY_LUNAR_HARP:
    case KEY_BLACK_MATERIA:
    case KEY_KEY_TO_ANCIENTS:
    case KEY_SNOWBOARD:
        return MOMENT_MIDGAR_ESCAPE;
    default:
        return MOMENT_GAME_START;
    }
}

int FieldPickupRandomizer_ff7tk::getKeyItemMaxMoment(quint32 keyItemId)
{
    switch (keyItemId) {
    case KEY_COTTON_DRESS: case KEY_SATIN_DRESS: case KEY_SILK_DRESS:
    case KEY_WIG: case KEY_DYED_WIG: case KEY_BLONDE_WIG:
    case KEY_GLASS_TIARA: case KEY_RUBY_TIARA: case KEY_DIAMOND_TIARA:
    case KEY_COLOGNE: case KEY_FLOWER_COLOGNE: case KEY_SEXY_COLOGNE:
    case KEY_MEMBERS_CARD: case KEY_LINGERIE: case KEY_MYSTERY_PANTIES:
    case KEY_BIKINI_BRIEFS: case KEY_PHARMACY_COUPON: case KEY_DISINFECTANT:
    case KEY_DEODORANT: case KEY_DIGESTIVE:
    case KEY_KEYCARD_60: case KEY_KEYCARD_62: case KEY_KEYCARD_65:
    case KEY_KEYCARD_66: case KEY_KEYCARD_68:
    case KEY_MIDGAR_PARTS_1: case KEY_MIDGAR_PARTS_2: case KEY_MIDGAR_PARTS_3:
    case KEY_MIDGAR_PARTS_4: case KEY_MIDGAR_PARTS_5:
    case KEY_A_COUPON: case KEY_B_COUPON: case KEY_C_COUPON:
        return MOMENT_MIDGAR_ESCAPE - 1;
    default:
        return MOMENT_FOREVER;
    }
}

QPair<int, int> FieldPickupRandomizer_ff7tk::getStitmMomentWindow(const QString& fieldName, int scriptOffset)
{
    QString lower = fieldName.toLower();
    if (lower == "blin63_1") {
        if (scriptOffset < 10000)
            return {MOMENT_GAME_START, MOMENT_MIDGAR_ESCAPE - 1};
        return {MOMENT_MIDGAR_ESCAPE, MOMENT_FOREVER};
    }
    return {MOMENT_GAME_START, MOMENT_FOREVER};
}

bool FieldPickupRandomizer_ff7tk::requiresMirroredBitons(const QString& fieldName)
{
    static const QSet<QString> mirroredFields = {
        QStringLiteral("mkt_mens"),
        QStringLiteral("mkt_m"),
        QStringLiteral("mktpb"),
        QStringLiteral("mkt_s1")
    };
    return mirroredFields.contains(fieldName.trimmed().toLower());
}

FieldPickupRandomizer_ff7tk::WardrobeCategory
FieldPickupRandomizer_ff7tk::getWardrobeCategory(quint32 keyItemId)
{
    switch (keyItemId) {
    case KEY_COTTON_DRESS: case KEY_SATIN_DRESS: case KEY_SILK_DRESS:
        return WardrobeCategory::Dress;
    case KEY_WIG: case KEY_DYED_WIG: case KEY_BLONDE_WIG:
        return WardrobeCategory::Wig;
    case KEY_GLASS_TIARA: case KEY_RUBY_TIARA: case KEY_DIAMOND_TIARA:
        return WardrobeCategory::Tiara;
    case KEY_COLOGNE: case KEY_FLOWER_COLOGNE: case KEY_SEXY_COLOGNE:
        return WardrobeCategory::Cologne;
    case KEY_LINGERIE: case KEY_MYSTERY_PANTIES: case KEY_BIKINI_BRIEFS:
        return WardrobeCategory::Underwear;
    default:
        return WardrobeCategory::None;
    }
}

QString FieldPickupRandomizer_ff7tk::wardrobeCategoryName(WardrobeCategory category)
{
    switch (category) {
    case WardrobeCategory::Dress:     return QStringLiteral("Dress");
    case WardrobeCategory::Wig:       return QStringLiteral("Wig");
    case WardrobeCategory::Tiara:     return QStringLiteral("Tiara");
    case WardrobeCategory::Cologne:   return QStringLiteral("Cologne");
    case WardrobeCategory::Underwear: return QStringLiteral("Underwear");
    default:                          return QStringLiteral("None");
    }
}

QPair<int, int> FieldPickupRandomizer_ff7tk::getFieldMomentWindow(const QString& fieldName)
{
    int sphere = getFieldSphere(fieldName);

    if (sphere >= 0 && sphere <= 7)
        return {MOMENT_GAME_START, MOMENT_MIDGAR_ESCAPE - 1};

    if (sphere >= 8 && sphere <= 99)
        return {MOMENT_MIDGAR_ESCAPE, MOMENT_FOREVER};

    return {MOMENT_GAME_START, MOMENT_FOREVER};
}

QString FieldPickupRandomizer_ff7tk::getKeyItemName(quint16 saveOffset, quint8 bit)
{
    if (saveOffset == 0x0BE4) {
        static const QStringList n = {"Cotton Dress","Satin Dress","Silk Dress","Wig","Dyed Wig","Blonde Wig","Glass Tiara","Ruby Tiara"};
        if (bit < 8) return n[bit];
    } else if (saveOffset == 0x0BE5) {
        static const QStringList n = {"Diamond Tiara","Cologne","Flower Cologne","Sexy Cologne","Member's Card","Lingerie","Mystery Panties","Bikini Briefs"};
        if (bit < 8) return n[bit];
    } else if (saveOffset == 0x0BE6) {
        static const QStringList n = {"Pharmacy Coupon","Disinfectant","Deodorant","Digestive","Huge Materia (Fort Condor)","Huge Materia (Corel)","Huge Materia (Underwater)","Huge Materia (Rocket)"};
        if (bit < 8) return n[bit];
    } else if (saveOffset == 0x0BE7) {
        static const QStringList n = {"Key to Ancients","Letter to Daughter","Letter to Wife","Lunar Harp","Basement Key","Key to Sector 5","Keycard 60","Keycard 62"};
        if (bit < 8) return n[bit];
    } else if (saveOffset == 0x0BE8) {
        static const QStringList n = {"Keycard 65","Keycard 66","Keycard 68","Midgar Parts","Midgar Parts","Midgar Parts","Midgar Parts","Midgar Parts"};
        if (bit < 8) return n[bit];
    } else if (saveOffset == 0x0BE9) {
        static const QStringList n = {"PHS","Gold Ticket","Keystone","Leviathan Scales","Glacier Map","A Coupon","B Coupon","C Coupon"};
        if (bit < 8) return n[bit];
    } else if (saveOffset == 0x0BEA) {
        static const QStringList n = {"Black Materia","Mythril","Snowboard","Unknown","Unknown","Unknown","Unknown","Unknown"};
        if (bit < 8) return n[bit];
    }
    return QString("KeyItem@0x%1 bit%2").arg(saveOffset, 0, 16).arg(bit);
}

void FieldPickupRandomizer_ff7tk::collectKeyItemsAndStitm(
    const QByteArray& fieldData, int fileIndex, const QString& fieldName,
    QMap<quint32, GlobalKeyItem>& uniqueKeyItems,
    QVector<GlobalStitmLocation>& stitmLocations,
    QTextStream& debugStream)
{
    if (fieldData.size() < 42) return;

    // Exclude debug maps entirely from key item randomization
    if (fieldName.startsWith("blackbg")) return;

    // Exclude md1stin: variable-gated entities make placed key items unobtainable
    if (fieldName.toLower() == "md1stin") return;

    // Exclude onna_5 from key item randomization, I dont know why this field keeps triggering the STITM detection
    if (fieldName == "onna_5") return;

    QByteArray decompressed = LZS::decompressAllWithHeader(fieldData);
    if (decompressed.isEmpty()) return;

    const int fileSize = decompressed.size();
    const int FIELD_HEADER_SIZE = 6 + 9 * 4;
    if (fileSize < FIELD_HEADER_SIZE) return;

    quint32 sectionPositions[9];
    memcpy(sectionPositions, decompressed.constData() + 6, 9 * 4);
    quint32 sec0off = sectionPositions[0];
    quint32 sec1off = sectionPositions[1];
    if (sec0off + 4 >= static_cast<quint32>(fileSize) ||
        sec1off > static_cast<quint32>(fileSize) || sec1off <= sec0off + 4)
        return;

    int sec0DataStart = static_cast<int>(sec0off) + 4;
    quint16 posTexts;
    memcpy(&posTexts, decompressed.constData() + sec0DataStart + 4, 2);

    int scriptStart = sec0DataStart + 46;
    int scriptEnd   = sec0DataStart + posTexts;
    if (scriptStart >= scriptEnd || scriptEnd > fileSize) return;

    QPair<int, int> fieldWindow = getFieldMomentWindow(fieldName);

    for (int i = scriptStart; i < scriptEnd - 5; ++i) {
        quint8 opcode = static_cast<quint8>(decompressed.at(i));

        // STITM (0x58) – potential swap target for key items
        if (opcode == STITM_OPCODE && i + 4 < scriptEnd) {
            quint8 bankByte = static_cast<quint8>(decompressed.at(i + 1));
            if (bankByte == 0x00) {
                quint16 itemId;
                memcpy(&itemId, decompressed.constData() + i + 2, 2);
                quint8 qty = static_cast<quint8>(decompressed.at(i + 4));
                if (itemId <= MAX_ITEM_ID && qty >= 1 && qty <= 99) {
                    QPair<int, int> window = getStitmMomentWindow(fieldName, i);
                    int minMoment = std::max(fieldWindow.first, window.first);
                    int maxMoment = std::min(fieldWindow.second, window.second);
                    if (minMoment > maxMoment)
                        continue;
                    GlobalStitmLocation loc;
                    loc.fileIndex     = fileIndex;
                    loc.scriptOffset  = i;
                    loc.minGameMoment = minMoment;
                    loc.maxGameMoment = maxMoment;
                    loc.isBiton       = false;
                    stitmLocations.append(loc);
                }
            }
            i += 4;
            continue;
        }

        // BITON (0x82) – key item flag set
        if (opcode == BITON_OPCODE && i + 3 < scriptEnd) {
            quint8 bankByte = static_cast<quint8>(decompressed.at(i + 1));
            quint8 destBank = (bankByte >> 4) & 0x0F;
            quint8 srcBank  = bankByte & 0x0F;
            quint8 address  = static_cast<quint8>(decompressed.at(i + 2));
            quint8 bitNum   = static_cast<quint8>(decompressed.at(i + 3));

            if (destBank >= 1 && destBank <= 2 && srcBank == 0 &&
                address >= 0x40 && address <= 0x46 && bitNum <= 7) {
                quint32 uniqueId = (static_cast<quint32>(address) << 8) | bitNum;
                if (!uniqueKeyItems.contains(uniqueId)) {
                    GlobalKeyItem item;
                    item.fileIndex    = fileIndex;
                    item.scriptOffset = i;
                    item.bankByte     = bankByte;
                    item.address      = address;
                    item.bit          = bitNum;
                    uniqueKeyItems.insert(uniqueId, item);

                    quint16 saveOffset = 0x0BA4 + address;
                    debugStream << "  KEY_ITEM: '" << getKeyItemName(saveOffset, bitNum)
                                << "' in " << fieldName << " @" << i << "\n";

                    GlobalStitmLocation bitonLoc;
                    bitonLoc.fileIndex     = fileIndex;
                    bitonLoc.scriptOffset  = i;
                    bitonLoc.minGameMoment = fieldWindow.first;
                    bitonLoc.maxGameMoment = fieldWindow.second;
                    bitonLoc.isBiton       = true;
                    stitmLocations.append(bitonLoc);
                }
            }
            i += 3;
            continue;
        }
    }
}

QMap<QString, FieldPickupRandomizer_ff7tk::KeyItemFieldMod>
FieldPickupRandomizer_ff7tk::performKeyItemSwaps(
    QMap<quint32, GlobalKeyItem>& uniqueKeyItems,
    QVector<GlobalStitmLocation>& stitmLocations,
    const QStringList& allFileNames,
    QTextStream& debugStream)
{
    debugStream << "\n=== KEY ITEM SWAP (SPHERE-AWARE) ===\n";
    debugStream << "Unique key items: " << uniqueKeyItems.size() << "\n";
    debugStream << "STITM locations: " << stitmLocations.size() << "\n\n";

    // Build sphere-aware STITM location list
    struct SphereStitm {
        int fileIndex;
        int scriptOffset;
        QString fieldName;
        int sphere;
        int minMoment;
        int maxMoment;
        bool isBiton;
    };
    QVector<SphereStitm> sphereLocs;
    for (const auto& loc : stitmLocations) {
        SphereStitm s;
        s.fileIndex    = loc.fileIndex;
        s.scriptOffset = loc.scriptOffset;
        s.fieldName    = allFileNames[loc.fileIndex];
        s.sphere       = getFieldSphere(s.fieldName);
        s.minMoment    = loc.minGameMoment;
        s.maxMoment    = loc.maxGameMoment;
        s.isBiton      = loc.isBiton;
        sphereLocs.append(s);
    }

    std::array<bool, static_cast<int>(WardrobeCategory::Underwear) + 1> wardrobeCategoryUsed{};
    wardrobeCategoryUsed.fill(false);

    // Sort key items by maxSphere (most restrictive first)
    QVector<QPair<quint32, GlobalKeyItem>> sorted;
    for (auto it = uniqueKeyItems.begin(); it != uniqueKeyItems.end(); ++it)
        sorted.append({it.key(), it.value()});
    std::sort(sorted.begin(), sorted.end(),
              [](const QPair<quint32, GlobalKeyItem>& a,
                 const QPair<quint32, GlobalKeyItem>& b) {
                  return getKeyItemMaxSphere(a.first) < getKeyItemMaxSphere(b.first);
              });

    // Compute all placements (no LGP modification — that happens in the per-file loop)
    QMap<QString, KeyItemFieldMod> fieldMods;
    QSet<int> usedLocIndices;
    int placed = 0;

    for (const auto& kv : sorted) {
        quint32 keyItemId     = kv.first;
        const GlobalKeyItem& keyItem = kv.second;
        int minSphere = getKeyItemMinSphere(keyItemId);
        int maxSphere = getKeyItemMaxSphere(keyItemId);
        int minMoment = getKeyItemMinMoment(keyItemId);
        int maxMoment = getKeyItemMaxMoment(keyItemId);
        WardrobeCategory wardrobeCategory = getWardrobeCategory(keyItemId);
        int wardrobeIndex = static_cast<int>(wardrobeCategory);

        quint16 saveOffset = 0x0BA4 + keyItem.address;
        QString keyName = getKeyItemName(saveOffset, keyItem.bit);

        if (wardrobeCategory != WardrobeCategory::None && wardrobeCategoryUsed[wardrobeIndex]) {
            debugStream << "  SKIP: '" << keyName << "' – wardrobe category '"
                        << wardrobeCategoryName(wardrobeCategory)
                        << "' already satisfied\n";
            continue;
        }

        QVector<int> validIndices;
        for (int i = 0; i < sphereLocs.size(); ++i) {
            if (usedLocIndices.contains(i)) continue;
            const SphereStitm& candidate = sphereLocs[i];
            int s = candidate.sphere;
            if (s < minSphere || s > maxSphere)
                continue;
            if (candidate.maxMoment < minMoment || candidate.minMoment > maxMoment)
                continue;
            validIndices.append(i);
        }

        if (validIndices.isEmpty()) {
            debugStream << "  SKIP: '" << keyName << "' – no valid STITM in spheres "
                        << minSphere << "-" << maxSphere
                        << ", moments " << minMoment << "-" << maxMoment << "\n";
            continue;
        }

        QVector<int> filteredIndices = validIndices;

        // Pre-filter: if blin63_1 already has a different key item placed,
        // exclude all blin63_1 slots so we never conflict and silently drop items.
        if (!fieldMods["blin63_1"].placements.isEmpty()) {
            QVector<int> noBlin63;
            for (int i : filteredIndices) {
                if (sphereLocs[i].fieldName.toLower() != "blin63_1")
                    noBlin63.append(i);
            }
            if (!noBlin63.isEmpty())
                filteredIndices = noBlin63;
        }

        int pick = filteredIndices[m_rng.bounded(filteredIndices.size())];
        usedLocIndices.insert(pick);
        const SphereStitm& target = sphereLocs[pick];

        {
            // Record NOP-out of original BITON in source field
            QString srcFieldName = allFileNames[keyItem.fileIndex];
            fieldMods[srcFieldName].bitonNopOffsets.append(keyItem.scriptOffset);

            // Record new BITON placement in target field
            KeyItemPlacement p;
            p.keyItem      = keyItem;
            p.keyName      = keyName;
            p.targetOffset = target.scriptOffset;
            p.targetIsBiton = target.isBiton;
            fieldMods[target.fieldName].placements.append(p);

            if (target.isBiton && requiresMirroredBitons(target.fieldName)) {
                for (int j = 0; j < sphereLocs.size(); ++j) {
                    if (j == pick) continue;
                    const SphereStitm& mirror = sphereLocs[j];
                    if (!mirror.isBiton) continue;
                    if (!mirror.fieldName.compare(target.fieldName, Qt::CaseInsensitive) == 0)
                        continue;
                    if (usedLocIndices.contains(j)) continue;
                    usedLocIndices.insert(j);
                    KeyItemPlacement mirrorPlacement = p;
                    mirrorPlacement.targetOffset = mirror.scriptOffset;
                    mirrorPlacement.targetIsBiton = true;
                    fieldMods[mirror.fieldName].placements.append(mirrorPlacement);
                    debugStream << "    MIRROR: '" << keyName << "' duplicated in "
                                << mirror.fieldName << " @" << mirror.scriptOffset << "\n";
                }
            }

            if (wardrobeCategory != WardrobeCategory::None)
                wardrobeCategoryUsed[wardrobeIndex] = true;

            placed++;
            debugStream << "  PLACED: '" << keyName << "' -> " << target.fieldName
                        << " (sphere " << target.sphere << ") @" << target.scriptOffset
                        << "  [src: " << srcFieldName << " @" << keyItem.scriptOffset << "]\n";
        }
    }

    debugStream << "\nKey items placed: " << placed << " / " << uniqueKeyItems.size() << "\n";

    // Modifications are returned; they will be applied in the per-file loop
    // alongside STITM/SMTRA randomization so nothing gets overwritten.
    return fieldMods;
}

// ============================================================================
// Item pool management
// ============================================================================

void FieldPickupRandomizer_ff7tk::initializeItemPools()
{
    buildItemPools();
    buildMateriaPool();
}

void FieldPickupRandomizer_ff7tk::buildItemPools()
{
    m_commonItems.clear();
    m_uncommonItems.clear();
    m_rareItems.clear();
    m_veryRareItems.clear();

    // ----- Consumables (IDs 0-31) – common tier -----------------------------
    // Potion=0, Hi-Potion=1, X-Potion=2, Ether=3, Turbo Ether=4,
    // Elixir=5, Megalixir=6, Phoenix Down=7, Antidote=8, Soft=9,
    // Maiden's Kiss=10, Cornucopia=11, Echo Screen=12, Hyper=13,
    // Tranquilizer=14, Remedy=15, Smoke Bomb=16, Speed Drink=17,
    // Hero Drink=18, Vaccine=19, Grenade=20, Shrapnel=21,
    // Right Arm=22, Deadly Waste=23, M-Tentacles=24, Stardust=25,
    // Vampire Fang=26, Ghost Hand=27, Spider Web=28, Dream Powder=29,
    // Mute Mask=30, War Gong=31
    for (quint16 i = 0; i <= 31; ++i) {
        m_commonItems.append(i);
    }

    // ----- Battle items (IDs 32-63) – uncommon tier -------------------------
    for (quint16 i = 32; i <= 63; ++i) {
        m_uncommonItems.append(i);
    }

    // ----- Equipment: weapons + armour (IDs 128-255) – rare tier ------------
    for (quint16 i = 128; i <= 255; ++i) {
        m_rareItems.append(i);
    }

    // ----- Accessories (IDs 256-319) – very rare tier -----------------------
    for (quint16 i = 256; i <= MAX_ITEM_ID; ++i) {
        m_veryRareItems.append(i);
    }

    qDebug() << "Item pools built:"
             << "common=" << m_commonItems.size()
             << "uncommon=" << m_uncommonItems.size()
             << "rare=" << m_rareItems.size()
             << "veryRare=" << m_veryRareItems.size();
}

quint16 FieldPickupRandomizer_ff7tk::getRandomItem(int rarityMode)
{
    QVector<quint16> pool;

    switch (rarityMode) {
    case 0:  // balanced – mostly common
        pool = m_commonItems;
        break;
    case 1:  // random – common + uncommon
        pool = m_commonItems + m_uncommonItems;
        break;
    case 2:  // high-tier
        pool = m_commonItems + m_uncommonItems + m_rareItems + m_veryRareItems;
        break;
    default:
        pool = m_commonItems;
        break;
    }

    if (pool.isEmpty()) return 0;
    return pool[m_rng.bounded(pool.size())];
}

void FieldPickupRandomizer_ff7tk::buildMateriaPool()
{
    m_materiaPool.clear();

    // FF7 KERNEL materia byte values (see ff7tk FF7Materia.h).  Valid bytes
    // are 0x00..0x5A with gaps at 0x16, 0x26, 0x2D-0x2F, 0x3F, 0x42-0x43
    // (placeholder slots that render as blank/garbage materia in-game), so
    // skip them when populating the random pool.
    static const QSet<quint8> placeholders = {
        0x16, 0x26, 0x2D, 0x2E, 0x2F, 0x3F, 0x42, 0x43
    };
    for (quint8 i = 0; i <= MAX_MATERIA_ID; ++i) {
        if (placeholders.contains(i)) continue;
        m_materiaPool.append(i);
    }

    qDebug() << "Materia pool built:" << m_materiaPool.size() << "materia";
}

quint8 FieldPickupRandomizer_ff7tk::getRandomMateria()
{
    if (m_materiaPool.isEmpty()) return 0;
    return m_materiaPool[m_rng.bounded(m_materiaPool.size())];
}

QString FieldPickupRandomizer_ff7tk::getMateriaName(quint8 materiaId) const
{
    // Authoritative table from ff7tk FF7Materia.h (KERNEL.bin layout).
    // Note the gaps at 0x16, 0x26, 0x2D-0x2F, 0x3F, 0x42-0x43 (placeholder
    // slots, never produced by vanilla SMTRA/AP).
    static const QMap<quint8, QString> names = {
        {0x00, "MP Plus"},         {0x01, "HP Plus"},         {0x02, "Speed Plus"},
        {0x03, "Magic Plus"},      {0x04, "Luck Plus"},       {0x05, "EXP Plus"},
        {0x06, "Gil Plus"},        {0x07, "Enemy Away"},      {0x08, "Enemy Lure"},
        {0x09, "Chocobo Lure"},    {0x0A, "Pre-emptive"},     {0x0B, "Long Range"},
        {0x0C, "Mega All"},        {0x0D, "Counter Attack"},  {0x0E, "Slash-All"},
        {0x0F, "Double Cut"},
        {0x10, "Cover"},           {0x11, "Underwater"},      {0x12, "HP <-> MP"},
        {0x13, "W-Magic"},         {0x14, "W-Summon"},        {0x15, "W-Item"},
        {0x17, "All"},             {0x18, "Counter"},         {0x19, "Magic Counter"},
        {0x1A, "MP Turbo"},        {0x1B, "MP Absorb"},       {0x1C, "HP Absorb"},
        {0x1D, "Elemental"},       {0x1E, "Added Effect"},    {0x1F, "Sneak Attack"},
        {0x20, "Final Attack"},    {0x21, "Added Cut"},       {0x22, "Steal-As-Well"},
        {0x23, "Quadra Magic"},    {0x24, "Steal"},           {0x25, "Sense"},
        {0x27, "Throw"},           {0x28, "Morph"},           {0x29, "Deathblow"},
        {0x2A, "Manipulate"},      {0x2B, "Mime"},            {0x2C, "Enemy Skill"},
        {0x30, "Master Command"},  {0x31, "Fire"},            {0x32, "Ice"},
        {0x33, "Earth"},           {0x34, "Lightning"},       {0x35, "Restore"},
        {0x36, "Heal"},            {0x37, "Revive"},          {0x38, "Seal"},
        {0x39, "Mystify"},         {0x3A, "Transform"},       {0x3B, "Exit"},
        {0x3C, "Poison"},          {0x3D, "Gravity"},         {0x3E, "Barrier"},
        {0x40, "Comet"},           {0x41, "Time"},
        {0x44, "Destruct"},        {0x45, "Contain"},         {0x46, "Full Cure"},
        {0x47, "Shield"},          {0x48, "Ultima"},          {0x49, "Master Magic"},
        {0x4A, "Choco/Mog"},       {0x4B, "Shiva"},           {0x4C, "Ifrit"},
        {0x4D, "Ramuh"},           {0x4E, "Titan"},           {0x4F, "Odin"},
        {0x50, "Leviathan"},       {0x51, "Bahamut"},         {0x52, "Kujata"},
        {0x53, "Alexander"},       {0x54, "Phoenix"},         {0x55, "Neo Bahamut"},
        {0x56, "Hades"},           {0x57, "Typhon"},          {0x58, "Bahamut ZERO"},
        {0x59, "Knights of the Round"}, {0x5A, "Master Summon"},
    };

    auto it = names.find(materiaId);
    if (it != names.end()) return it.value();
    return QString("Materia_0x%1").arg(materiaId, 2, 16, QChar('0')).toUpper();
}

// ============================================================================
// Helpers
// ============================================================================

QString FieldPickupRandomizer_ff7tk::findFlevelPath() const
{
    if (!m_parent) return QString();

    QString ff7Path = m_parent->getFF7Path();
    QStringList candidates = {
        ff7Path + "/data/field/flevel.lgp",
        ff7Path + "/data/flevel/flevel.lgp",
        ff7Path + "/field/flevel.lgp",
    };

    for (const QString& p : candidates) {
        if (QFile::exists(p)) return p;
    }

    // Also check if user placed it in the output folder already
    QString outputPath = m_parent->getOutputPath();
    QStringList outputCandidates = {
        outputPath + "/data/field/flevel.lgp",
        outputPath + "/data/flevel/flevel.lgp",
    };
    for (const QString& p : outputCandidates) {
        if (QFile::exists(p)) return p;
    }

    return QString();
}

QString FieldPickupRandomizer_ff7tk::getItemName(quint16 itemId) const
{
    // Use ff7tk's authoritative item name table
    QString name = FF7Item::name(itemId);
    if (!name.isEmpty()) return name;
    return QString("Item_%1").arg(itemId);
}
