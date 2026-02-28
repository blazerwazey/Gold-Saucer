#include "FieldPickupRandomizer_ff7tk.h"
#include "Randomizer.h"
#include "Config.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QTextStream>
#include <QFileInfo>
#include <QDateTime>
#include <LZS>
#include <ff7tk/data/FF7Text.h>
#include <ff7tk/data/FF7Item.h>
#include <algorithm>

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

    // --- key item randomization (global pass, before per-file processing) ---
    bool keyItemEnabled = m_parent && m_parent->m_config.getKeyItemRandomization();
    if (debugOk) {
        debugStream << "Key Item Randomization: "
                    << (keyItemEnabled ? "ENABLED" : "DISABLED") << "\n\n";
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
            if (fn == "onna_5") continue; // Exclude onna_5 from key item collection
            if (fn == "mkt_w") continue; // Exclude mkt_w from key item collection

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

    for (const QString& fileName : allFiles) {
        if (fileName.startsWith("blackbg")) continue;
        if (fileName == "onna_5") continue; // Exclude onna_5 from randomization

        QByteArray fieldData = lgp.fileData(fileName);
        if (fieldData.isEmpty()) continue;

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

    // --- summary ------------------------------------------------------------
    if (debugOk) {
        debugStream << "\n=== Summary ===\n";
        debugStream << "Files with STITM changes: " << filesWithChanges << "\n";
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
            if (p.targetOffset + 4 < decompressed.size()) {
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
                decompressed[p.targetOffset + 4] = static_cast<char>(0x5F); // NOP
                
                // Debug: Show new bytes after replacement
                QString newBytes;
                for (int i = 0; i < 8 && p.targetOffset + i < decompressed.size(); ++i) {
                    newBytes += QString("%1 ").arg(static_cast<quint8>(decompressed[p.targetOffset + i]), 2, 16, QChar('0')).toUpper();
                }
                debugStream << "  KEY_ITEM REPLACED  @" << p.targetOffset
                            << " new:      " << newBytes << "\n";
                debugStream << "  KEY_ITEM BITON @" << p.targetOffset
                            << " -> " << p.keyName << "\n";
                totalMods++;
            }
            modifications.append(
                OpcodeModification(p.targetOffset,
                                   QString("Key Item: %1").arg(p.keyName),
                                   false));
        }
    }

    // --- STITM (items) ------------------------------------------------------
    // Key item BITONs are already written, so scan won't match those offsets.
    QVector<STITMInfo> stitmCandidates = scanForSTITM(decompressed, fieldName, debugStream);

    // Special case: md1stin has multiple entities gated by a variable, each
    // with 2 STITMs.  Only one entity fires, so randomize 2 items once and
    // apply the same pair to every entity so all items are obtainable.
    bool isMd1stin = (fieldName.toLower() == "md1stin");
    
    // Special case: mkt_w has multiple entities that need to give the same items
    // to ensure all items are obtainable regardless of which entity fires
    bool isMktW = (fieldName.toLower() == "mkt_w");

    // Collect valid candidates first
    QVector<int> validIndices;
    for (int idx = 0; idx < stitmCandidates.size(); ++idx) {
        if (validateSTITM(stitmCandidates[idx]))
            validIndices.append(idx);
    }

    if (isMd1stin && validIndices.size() >= 2) {
        // Pick 2 items that every entity will share
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
        // Pick items that every entity will share (same logic as md1stin)
        QVector<quint16> sharedItems;
        for (int i = 0; i < validIndices.size(); ++i) {
            sharedItems.append(getRandomItem(1));
        }
        debugStream << "  mkt_w special: syncing all entities to items\n";
        for (int i = 0; i < sharedItems.size(); ++i) {
            debugStream << "    Item " << i << ": " << getItemName(sharedItems[i]) << " (" << sharedItems[i] << ")\n";
        }

        for (int v = 0; v < validIndices.size(); ++v) {
            STITMInfo& info = stitmCandidates[validIndices[v]];
            quint16 newItemID = sharedItems[v % sharedItems.size()];
            if (applySTITMRandomization(info, decompressed, newItemID, debugStream)) {
                modifications.append(OpcodeModification(info.offset, getItemName(newItemID), false));
                totalMods++;
            }
        }
    } else {
        // Normal per-STITM randomization
        for (int idx : validIndices) {
            STITMInfo& info = stitmCandidates[idx];
            quint16 newItemID = getRandomItem(1);
            if (applySTITMRandomization(info, decompressed, newItemID, debugStream)) {
                modifications.append(OpcodeModification(info.offset, getItemName(newItemID), false));
                totalMods++;
            }
        }
    }

    // --- SMTRA (materia) ----------------------------------------------------
    QVector<SMTRAInfo> smtraCandidates = scanForSMTRA(decompressed, fieldName, debugStream);
    for (SMTRAInfo& info : smtraCandidates) {
        if (!validateSMTRA(info)) continue;
        quint8 newMateriaID = getRandomMateria();
        if (applySMTRARandomization(info, decompressed, newMateriaID, debugStream)) {
            modifications.append(OpcodeModification(info.offset, getMateriaName(newMateriaID), true));
            totalMods++;
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
        if (recompressed.isEmpty()) {
            debugStream << fieldName << ": LZS recompression failed!\n";
            return false;
        }
        fieldData = recompressed;
        debugStream << "  >> " << fieldName << ": modified "
                    << totalMods << " opcode(s)\n\n";
    }
    return totalMods > 0;
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

        // Search forward (up to 500 bytes)
        {
            int searchEnd = qMin(mod.opcodeOffset + 500, scriptAbsEnd - 2);
            for (int pos = mod.opcodeOffset; pos < searchEnd; ++pos) {
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
        "mds7st1","mds7st2","mds7st3","mds7_w1","mds7_w2","mds7_w3",
        "md1stin","md1_1","md1_2","nmkin_1","nmkin_2","nmkin_3","nmkin_4","nmkin_5",
        "nrthmk","southmk1","southmk2",
        "md8_1","md8_2","md8_3","md8_4","md8brdg1","md8brdg2",
        "mds7plr1","mds7plr2","tin_1","tin_2","tin_3","tin_4",
        "7min1","7min2","7min3","sector1","sector2"
    };
    static const QSet<QString> sphere1 = {
        "mkt_ia","mkt_s1","mkt_s2","mkt_s3","mkt_w","mkt_mens",
        "mkt_m","mkt_pub","mktpb","mkt_inn",
        "onna_1","onna_2","onna_3","onna_4","onna_5","onna_51","onna_52",
        "colne_1","colne_2","colne_3","colne_4","colne_5","colne_6",
        "mds5_1","mds5_2","mds5_3","mds5_4","mds5_5","church","church2"
    };
    static const QSet<QString> sphere2 = {
        "blin1","blin2_1","blin2_2","blin2_3","blin59"
    };
    static const QSet<QString> sphere3 = { "blin60" };
    static const QSet<QString> sphere4 = {
        "blin61","blin62_1","blin62_2","blin63_1","blin63_2","blin64"
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
        "condor1","condor2","convil_1","convil_2","convil_3","convil_4",
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
        "delmin1","delmin2","delmin3","delmin4","delmin5","delmin6",
        "delmin7","delmin8","delmin9","delmin10","delmin11","delmin12"
    };
    static const QSet<QString> sphere12 = {
        "bonevil","bonevil2","slfrst_1","slfrst_2","slfrst_3"
    };
    static const QSet<QString> sphere13 = {
        "ancnt1","ancnt2","ancnt3","ancnt4",
        "anfrst_1","anfrst_2","anfrst_3","anfrst_4","anfrst_5",
        "losin1","losin2","losin3","losinn"
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
        return 0;
    case KEY_KEYCARD_62:  return 3;
    case KEY_KEYCARD_65:  return 4;
    case KEY_KEYCARD_66:  return 5;
    case KEY_KEYCARD_68:  return 6;
    case KEY_GOLD_TICKET: case KEY_KEYSTONE: case KEY_LUNAR_HARP:
    case KEY_SNOWBOARD:   return 8;
    case KEY_BLACK_MATERIA: return 11;
    case KEY_KEY_TO_ANCIENTS: return 12;
    case KEY_A_COUPON: case KEY_B_COUPON: case KEY_C_COUPON:
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
    case KEY_BIKINI_BRIEFS: case KEY_PHARMACY_COUPON: case KEY_DISINFECTANT:
    case KEY_DEODORANT: case KEY_DIGESTIVE:
        return 1;
    case KEY_KEYCARD_60:  return 2;
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
        return 3; // Available before blin63_1 (sphere 4)
    default: return 99;
    }
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

    // Exclude onna_5 from key item randomization
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
                    stitmLocations.append({fileIndex, i});
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
    struct SphereStitm { int fileIndex; int scriptOffset; QString fieldName; int sphere; };
    QVector<SphereStitm> sphereLocs;
    for (const auto& loc : stitmLocations) {
        SphereStitm s;
        s.fileIndex    = loc.fileIndex;
        s.scriptOffset = loc.scriptOffset;
        s.fieldName    = allFileNames[loc.fileIndex];
        s.sphere       = getFieldSphere(s.fieldName);
        sphereLocs.append(s);
    }

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

        quint16 saveOffset = 0x0BA4 + keyItem.address;
        QString keyName = getKeyItemName(saveOffset, keyItem.bit);

        QVector<int> validIndices;
        for (int i = 0; i < sphereLocs.size(); ++i) {
            if (usedLocIndices.contains(i)) continue;
            int s = sphereLocs[i].sphere;
            if (s >= minSphere && s <= maxSphere)
                validIndices.append(i);
        }

        if (validIndices.isEmpty()) {
            debugStream << "  SKIP: '" << keyName << "' – no valid STITM in spheres "
                        << minSphere << "-" << maxSphere << "\n";
            continue;
        }

        QVector<int> filteredIndices = validIndices;

        int pick = filteredIndices[m_rng.bounded(filteredIndices.size())];
        usedLocIndices.insert(pick);
        const SphereStitm& target = sphereLocs[pick];
        
        // Special handling for blin63_1: ensure entity consistency
        bool skipPlacement = false;
        if (target.fieldName.toLower() == "blin63_1") {
            // Check if we already placed a different key item in blin63_1
            // If so, skip this placement to maintain entity consistency
            for (const auto& existingPlacement : fieldMods[target.fieldName].placements) {
                if (existingPlacement.keyName != keyName) {
                    debugStream << "  SKIP: '" << keyName << "' – blin63_1 already has '" 
                                << existingPlacement.keyName << "', maintaining entity consistency\n";
                    usedLocIndices.remove(pick);
                    skipPlacement = true;
                    break;
                }
            }
        }

        if (!skipPlacement) {
            // Record NOP-out of original BITON in source field
            QString srcFieldName = allFileNames[keyItem.fileIndex];
            fieldMods[srcFieldName].bitonNopOffsets.append(keyItem.scriptOffset);

            // Record new BITON placement in target field
            KeyItemPlacement p;
            p.keyItem      = keyItem;
            p.keyName      = keyName;
            p.targetOffset = target.scriptOffset;
            fieldMods[target.fieldName].placements.append(p);

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

    // FF7 materia IDs 0-90
    // 0-13:  Magic materia (Fire, Ice, Lightning, Earth, Poison, Gravity,
    //        Seal, Mystify, Transform, Time, Barrier, Comet, Contain, FullCure)
    // 14-25: Support materia
    // 26-38: Command materia
    // 39-48: Independent materia
    // 49-90: Summon materia + extras
    for (quint8 i = 0; i <= MAX_MATERIA_ID; ++i) {
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
    static const QMap<quint8, QString> names = {
        {0,  "MP Plus"},        {1,  "HP Plus"},        {2,  "Speed Plus"},
        {3,  "Magic Plus"},     {4,  "Luck Plus"},      {5,  "EXP Plus"},
        {6,  "Gil Plus"},       {7,  "Enemy Away"},     {8,  "Enemy Lure"},
        {9,  "Chocobo Lure"},   {10, "Pre-Emptive"},    {11, "Long Range"},
        {12, "Mega All"},       {13, "Counter Attack"}, {14, "Slash-All"},
        {15, "Double Cut"},     {16, "Cover"},          {17, "Underwater"},
        {18, "HP<->MP"},        {19, "W-Magic"},        {20, "W-Summon"},
        {21, "W-Item"},         {22, "All"},            {23, "Counter"},
        {24, "Magic Counter"},  {25, "MP Turbo"},       {26, "MP Absorb"},
        {27, "HP Absorb"},      {28, "Elemental"},      {29, "Added Effect"},
        {30, "Sneak Attack"},   {31, "Final Attack"},   {32, "Added Cut"},
        {33, "Steal as Well"},  {34, "Quadra Magic"},   {35, "Steal"},
        {36, "Sense"},          {37, "Throw"},          {38, "Morph"},
        {39, "Deathblow"},      {40, "Manipulate"},     {41, "Mime"},
        {42, "Enemy Skill"},    {43, "Master Command"}, {44, "Fire"},
        {45, "Ice"},            {46, "Lightning"},      {47, "Earth"},
        {48, "Poison"},         {49, "Gravity"},        {50, "Seal"},
        {51, "Mystify"},        {52, "Transform"},      {53, "Time"},
        {54, "Barrier"},        {55, "Comet"},          {56, "Contain"},
        {57, "FullCure"},       {58, "Master Magic"},   {59, "Shield"},
        {60, "Ultima"},         {61, "Restore"},        {62, "Revive"},
        {63, "Heal"},           {64, "Destruct"},       {65, "Exit"},
        {66, "Choco/Mog"},      {67, "Shiva"},          {68, "Ifrit"},
        {69, "Ramuh"},          {70, "Titan"},          {71, "Odin"},
        {72, "Leviathan"},      {73, "Bahamut"},        {74, "Kjata"},
        {75, "Alexander"},      {76, "Phoenix"},        {77, "Neo Bahamut"},
        {78, "Hades"},          {79, "Typhon"},         {80, "Bahamut ZERO"},
        {81, "Knights of Round"},{82, "Master Summon"},
    };

    auto it = names.find(materiaId);
    if (it != names.end()) return it.value();
    return QString("Materia_%1").arg(materiaId);
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
