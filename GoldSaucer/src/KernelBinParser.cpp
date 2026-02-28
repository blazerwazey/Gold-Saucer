#include "KernelBinParser.h"
#include "TextEncoder.h"
#include "KernelBinValidator.h"
#include "UserFeedback.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

KernelBinParser::KernelBinParser()
    : m_isValid(false)
    , m_isModified(false)
{
}

KernelBinParser::~KernelBinParser()
{
}

bool KernelBinParser::load(const QString& filePath)
{
    qDebug() << "Attempting to open KERNEL.BIN:" << filePath;
    
    // Check if file exists first
    if (!QFile::exists(filePath)) {
        m_lastError = "KERNEL.BIN does not exist: " + filePath;
        qDebug() << m_lastError;
        return false;
    }
    
    // Get file info
    QFileInfo fileInfo(filePath);
    qDebug() << "File exists. Size:" << fileInfo.size() << "bytes";
    qDebug() << "Is readable:" << fileInfo.isReadable();
    qDebug() << "Is writable:" << fileInfo.isWritable();
    
    // Try direct open first
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Direct open failed:" << file.errorString();
        qDebug() << "Attempting to copy to temp location...";
        
        // Copy to temp location (workaround for Windows file protection)
        QString tempPath = QDir::tempPath() + "/ff7_kernel_temp.bin";
        if (QFile::copy(filePath, tempPath)) {
            qDebug() << "Copied to temp location:" << tempPath;
            file.setFileName(tempPath);
            if (!file.open(QIODevice::ReadOnly)) {
                m_lastError = "Could not open temp copy: " + tempPath + " - " + file.errorString();
                qDebug() << m_lastError;
                return false;
            }
        } else {
            m_lastError = "Could not open KERNEL.BIN: " + filePath + " - Error: " + file.errorString();
            qDebug() << m_lastError;
            return false;
        }
    }
    
    m_rawData = file.readAll();
    file.close();
    
    if (m_rawData.isEmpty()) {
        m_lastError = "KERNEL.BIN is empty";
        qDebug() << m_lastError;
        return false;
    }
    
    qDebug() << "Loaded KERNEL.BIN:" << filePath << "Size:" << m_rawData.size() << "bytes";
    
    // Validate KERNEL.BIN format before parsing
    KernelBinValidator::ValidationReport validation = KernelBinValidator::validateKernelBin(m_rawData);
    if (!validation.isValid) {
        m_lastError = "KERNEL.BIN validation failed: " + validation.errors.join(", ");
        qDebug() << m_lastError;
        
        // Show user-friendly error message
        UserFeedback& feedback = UserFeedback::instance();
        feedback.handleKernelBinError("load", m_lastError, filePath);
        
        for (const QString& warning : validation.warnings) {
            qWarning() << "Validation warning:" << warning;
        }
        return false;
    }
    
    // Log validation info
    qDebug() << "KERNEL.BIN validation passed:";
    qDebug() << "  Version:" << validation.version;
    qDebug() << "  Sections:" << validation.sectionCount;
    for (auto it = validation.sectionInfo.begin(); it != validation.sectionInfo.end(); ++it) {
        qDebug() << "  " << it.value();
    }
    
    if (!parseKernelBin()) {
        return false;
    }
    
    m_isValid = true;
    m_isModified = false;
    return true;
}

bool KernelBinParser::save(const QString& filePath)
{
    if (!m_isModified) {
        qDebug() << "KERNEL.BIN not modified, skipping save";
        return true;
    }
    
    // Create backup before saving
    QString backupPath = KernelBinValidator::createBackup(filePath);
    if (backupPath.isEmpty()) {
        qWarning() << "Warning: Could not create backup of" << filePath;
        UserFeedback::instance().showWarning("Backup Warning", 
            "Could not create backup of KERNEL.BIN. Continue anyway?", 
            "Check file permissions and disk space.");
    } else {
        qDebug() << "Created backup:" << backupPath;
        UserFeedback::instance().showInfo("Backup Created", 
            QString("Backup saved to: %1").arg(backupPath));
    }
    
    if (!rebuildKernelBin()) {
        m_lastError = "Failed to rebuild KERNEL.BIN";
        qDebug() << m_lastError;
        return false;
    }
    
    // Validate the rebuilt KERNEL.BIN
    KernelBinValidator::ValidationReport validation = KernelBinValidator::validateKernelBin(m_rawData);
    if (!validation.isValid) {
        m_lastError = "Rebuilt KERNEL.BIN validation failed: " + validation.errors.join(", ");
        qDebug() << m_lastError;
        
        // Show user-friendly error and attempt recovery
        UserFeedback& feedback = UserFeedback::instance();
        feedback.handleKernelBinError("save and validate", m_lastError, filePath);
        
        // Restore from backup if available
        if (!backupPath.isEmpty()) {
            qDebug() << "Attempting to restore from backup...";
            if (KernelBinValidator::restoreBackup(filePath, backupPath)) {
                qDebug() << "Successfully restored from backup";
                feedback.showSuccess("Backup Restored", "Original file has been restored from backup.");
            } else {
                qCritical() << "Failed to restore from backup - original file may be corrupted";
                feedback.showError("Backup Restore Failed", 
                    "Failed to restore from backup. Original file may be corrupted.",
                    "Manually restore from backup folder if needed.");
            }
        }
        return false;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = "Could not open KERNEL.BIN for writing: " + filePath;
        qDebug() << m_lastError;
        return false;
    }
    
    qint64 bytesWritten = file.write(m_rawData);
    file.close();
    
    if (bytesWritten != m_rawData.size()) {
        m_lastError = "Could not write all data to KERNEL.BIN";
        qDebug() << m_lastError;
        return false;
    }
    
    qDebug() << "Successfully saved KERNEL.BIN:" << filePath;
    UserFeedback::instance().showSuccess("Text Replacement Complete", 
        "Item names have been successfully replaced in KERNEL.BIN.");
    
    // Cleanup old backups (keep only the most recent 5)
    KernelBinValidator::cleanupOldBackups(filePath, 5);
    
    return true;
}

bool KernelBinParser::parseKernelBin()
{
    // Check minimum size for header
    if (m_rawData.size() < 0x20) {
        m_lastError = "KERNEL.BIN too small for header";
        qDebug() << m_lastError;
        return false;
    }
    
    // Parse header
    // FF7 KERNEL.BIN format:
    // Offset 0x00: Magic (9 bytes) - "FF7KERNEL"
    // Offset 0x09: Version (1 byte)
    // Offset 0x0A: Section count (1 byte)
    // Offset 0x0B: Padding (1 byte)
    // Offset 0x0C: Section offsets (4 bytes each)
    
    // Check magic
    QByteArray magic = m_rawData.mid(0, 9);
    if (magic != "FF7KERNEL" && magic != "KERNEL2.B") {
        // Try alternate format (some versions)
        qDebug() << "Warning: Unexpected KERNEL.BIN magic:" << magic.toHex() << "- attempting parse anyway";
    }
    
    // Get section count
    quint8 sectionCount = static_cast<quint8>(m_rawData[0x0A]);
    if (sectionCount == 0 || sectionCount > 16) {
        m_lastError = "Invalid section count in KERNEL.BIN";
        qDebug() << m_lastError << "Count:" << sectionCount;
        return false;
    }
    
    qDebug() << "KERNEL.BIN sections:" << sectionCount;
    
    // Parse section offsets
    m_sectionOffsets.clear();
    for (int i = 0; i < sectionCount; i++) {
        quint32 offset = *reinterpret_cast<const quint32*>(m_rawData.constData() + 0x0C + (i * 4));
        m_sectionOffsets.append(offset);
        qDebug() << "Section" << i << "offset:" << QString::number(offset, 16);
    }
    
    // Parse shop section (section 5)
    if (!parseShopSection()) {
        return false;
    }
    
    // Parse item section (section 4) for text editing
    if (!parseItemSection()) {
        qDebug() << "Warning: Could not parse item section for text editing";
        // Continue anyway - text editing is optional
    }
    
    return true;
}

bool KernelBinParser::parseShopSection()
{
    // Shop data is in section 5
    if (SHOP_SECTION_INDEX >= m_sectionOffsets.size()) {
        m_lastError = "KERNEL.BIN does not have shop section (section 5)";
        qDebug() << m_lastError;
        return false;
    }
    
    quint32 shopSectionOffset = m_sectionOffsets[SHOP_SECTION_INDEX];
    quint32 nextSectionOffset = (SHOP_SECTION_INDEX + 1 < m_sectionOffsets.size()) 
        ? m_sectionOffsets[SHOP_SECTION_INDEX + 1] 
        : m_rawData.size();
    
    if (shopSectionOffset == 0 || shopSectionOffset >= m_rawData.size()) {
        m_lastError = "Invalid shop section offset";
        qDebug() << m_lastError << "Offset:" << QString::number(shopSectionOffset, 16);
        return false;
    }
    
    quint32 shopSectionSize = nextSectionOffset - shopSectionOffset;
    qDebug() << "Shop section: offset=" << QString::number(shopSectionOffset, 16) 
             << "size=" << shopSectionSize;
    
    // Parse shop data
    // Format:
    // - Shop count (1 byte) - optional, depends on version
    // - For each shop:
    //   - Shop ID (1 byte)
    //   - Item count (1 byte)
    //   - Item IDs (2 bytes each)
    
    m_shops.clear();
    int offset = shopSectionOffset;
    
    while (offset < shopSectionOffset + shopSectionSize - 2) {
        quint8 shopId = static_cast<quint8>(m_rawData[offset]);
        quint8 itemCount = static_cast<quint8>(m_rawData[offset + 1]);
        
        // Validate
        if (shopId == 0 || shopId >= MAX_SHOPS) {
            // End of shop list or invalid
            break;
        }
        
        if (itemCount == 0 || itemCount > MAX_ITEMS_PER_SHOP) {
            // Invalid item count
            offset += 2;
            continue;
        }
        
        // Check if we have enough data for all items
        if (offset + 2 + (itemCount * 2) > shopSectionOffset + shopSectionSize) {
            qDebug() << "Shop data extends beyond section bounds";
            break;
        }
        
        KernelShop shop;
        shop.shopId = shopId;
        
        // Read item IDs
        for (int i = 0; i < itemCount; i++) {
            quint16 itemId = *reinterpret_cast<const quint16*>(m_rawData.constData() + offset + 2 + (i * 2));
            shop.itemIds.append(itemId);
        }
        
        m_shops.append(shop);
        qDebug() << "Parsed shop" << shopId << "with" << itemCount << "items";
        
        // Move to next shop (2 bytes header + 2 bytes per item)
        offset += 2 + (itemCount * 2);
    }
    
    qDebug() << "Total shops parsed:" << m_shops.size();
    
    if (m_shops.isEmpty()) {
        m_lastError = "No shops found in KERNEL.BIN";
        qDebug() << m_lastError;
        return false;
    }
    
    return true;
}

QByteArray KernelBinParser::buildShopSection()
{
    QByteArray sectionData;
    
    for (const KernelShop& shop : m_shops) {
        // Shop ID
        sectionData.append(static_cast<char>(shop.shopId));
        // Item count
        sectionData.append(static_cast<char>(shop.itemIds.size()));
        // Item IDs
        for (quint16 itemId : shop.itemIds) {
            sectionData.append(reinterpret_cast<const char*>(&itemId), 2);
        }
    }
    
    // Add terminator (shop ID 0)
    sectionData.append('\0');
    
    return sectionData;
}

bool KernelBinParser::rebuildKernelBin()
{
    if (!m_isModified) {
        return true;
    }
    
    QByteArray newShopSection = buildShopSection();
    quint32 oldShopSectionOffset = m_sectionOffsets[SHOP_SECTION_INDEX];
    quint32 oldShopSectionSize = (SHOP_SECTION_INDEX + 1 < m_sectionOffsets.size()) 
        ? m_sectionOffsets[SHOP_SECTION_INDEX + 1] - oldShopSectionOffset
        : m_rawData.size() - oldShopSectionOffset;
    
    // Calculate size difference
    qint32 sizeDiff = newShopSection.size() - oldShopSectionSize;
    
    // Build new kernel data
    QByteArray newKernelData;
    
    // Copy header and sections before shop section
    newKernelData.append(m_rawData.left(oldShopSectionOffset));
    
    // Insert new shop section
    newKernelData.append(newShopSection);
    
    // Copy remaining sections
    if (oldShopSectionOffset + oldShopSectionSize < m_rawData.size()) {
        newKernelData.append(m_rawData.mid(oldShopSectionOffset + oldShopSectionSize));
    }
    
    // Update section offsets
    if (sizeDiff != 0) {
        for (int i = SHOP_SECTION_INDEX + 1; i < m_sectionOffsets.size(); i++) {
            m_sectionOffsets[i] += sizeDiff;
        }
        
        // Write updated offsets to header
        for (int i = 0; i < m_sectionOffsets.size(); i++) {
            quint32 offset = m_sectionOffsets[i];
            newKernelData.replace(0x0C + (i * 4), 4, reinterpret_cast<const char*>(&offset), 4);
        }
    }
    
    m_rawData = newKernelData;
    return true;
}

QVector<KernelShop> KernelBinParser::getShops() const
{
    return m_shops;
}

void KernelBinParser::setShops(const QVector<KernelShop>& shops)
{
    m_shops = shops;
    m_isModified = true;
}

KernelShop KernelBinParser::getShop(quint8 shopId) const
{
    for (const KernelShop& shop : m_shops) {
        if (shop.shopId == shopId) {
            return shop;
        }
    }
    return KernelShop();
}

bool KernelBinParser::hasShop(quint8 shopId) const
{
    for (const KernelShop& shop : m_shops) {
        if (shop.shopId == shopId) {
            return true;
        }
    }
    return false;
}

bool KernelBinParser::isValid() const
{
    return m_isValid;
}

QString KernelBinParser::getLastError() const
{
    return m_lastError;
}

int KernelBinParser::getShopCount() const
{
    return m_shops.size();
}

bool KernelBinParser::loadAllItemText()
{
    return parseItemSection();
}

bool KernelBinParser::parseItemSection()
{
    // Item data is in section 4
    if (ITEM_SECTION_INDEX >= m_sectionOffsets.size()) {
        m_lastError = "KERNEL.BIN does not have item section (section 4)";
        qDebug() << m_lastError;
        return false;
    }
    
    quint32 itemSectionOffset = m_sectionOffsets[ITEM_SECTION_INDEX];
    quint32 nextSectionOffset = (ITEM_SECTION_INDEX + 1 < m_sectionOffsets.size()) 
        ? m_sectionOffsets[ITEM_SECTION_INDEX + 1] 
        : m_rawData.size();
    
    if (itemSectionOffset == 0 || itemSectionOffset >= m_rawData.size()) {
        m_lastError = "Invalid item section offset";
        qDebug() << m_lastError << "Offset:" << QString::number(itemSectionOffset, 16);
        return false;
    }
    
    quint32 itemSectionSize = nextSectionOffset - itemSectionOffset;
    qDebug() << "Item section: offset=" << QString::number(itemSectionOffset, 16) 
             << "size=" << itemSectionSize;
    
    // Parse item data
    // Structure: 128 items × 28 bytes each
    // Offset 0-12: Item name (13 chars + null)
    // Offset 13-27: Item stats/description
    
    m_items.clear();
    
    for (int i = 0; i < MAX_ITEMS; i++) {
        quint32 itemOffset = itemSectionOffset + (i * 28);
        
        if (itemOffset + 28 > itemSectionOffset + itemSectionSize) {
            break;
        }
        
        KernelItem item;
        item.itemId = i;
        item.category = TextEncoder::getItemCategory(i);
        
        // Extract item name (first 13 bytes)
        QByteArray nameBytes = m_rawData.mid(itemOffset, 13);
        item.originalName = TextEncoder::ff7ToAscii(nameBytes);
        item.newName = item.originalName;
        item.isRandomized = false;
        
        // Extract description (remaining 15 bytes)
        QByteArray descBytes = m_rawData.mid(itemOffset + 13, 15);
        item.description = TextEncoder::ff7ToAscii(descBytes);
        
        m_items.append(item);
    }
    
    qDebug() << "Total items parsed:" << m_items.size();
    return true;
}

QVector<KernelItem> KernelBinParser::getAllItems() const
{
    return m_items;
}

bool KernelBinParser::replaceItemText(quint16 itemId, const QString& newName, ItemCategory category)
{
    // Find item by ID and category
    for (KernelItem& item : m_items) {
        if (item.itemId == itemId && item.category == category) {
            if (!TextEncoder::isValidItemName(newName, category)) {
                m_lastError = "Invalid item name: " + newName;
                return false;
            }
            
            item.newName = newName;
            item.isRandomized = true;
            m_isModified = true;
            qDebug() << "Replaced item" << itemId << "name:" << item.originalName << "->" << newName;
            return true;
        }
    }
    
    m_lastError = "Item not found: ID " + QString::number(itemId);
    qDebug() << m_lastError;
    return false;
}

bool KernelBinParser::replaceWeaponName(quint16 weaponId, const QString& newName)
{
    return replaceItemText(weaponId, newName, ItemCategory::Weapon);
}

bool KernelBinParser::replaceArmorName(quint16 armorId, const QString& newName)
{
    return replaceItemText(armorId, newName, ItemCategory::Armor);
}

bool KernelBinParser::replaceAccessoryName(quint16 accessoryId, const QString& newName)
{
    return replaceItemText(accessoryId, newName, ItemCategory::Accessory);
}

bool KernelBinParser::replaceMateriaName(quint16 materiaId, const QString& newName)
{
    return replaceItemText(materiaId, newName, ItemCategory::Materia);
}

bool KernelBinParser::replaceKeyItemName(quint16 keyItemId, const QString& newName)
{
    return replaceItemText(keyItemId, newName, ItemCategory::KeyItem);
}

bool KernelBinParser::saveAllItemText()
{
    if (!m_isModified) {
        qDebug() << "No item text modifications to save";
        return true;
    }
    
    return rebuildKernelBin();
}

QByteArray KernelBinParser::buildItemSection()
{
    QByteArray sectionData;
    
    for (const KernelItem& item : m_items) {
        // Write item name (13 bytes)
        QByteArray nameBytes = TextEncoder::asciiToFf7(item.newName);
        if (nameBytes.size() > 13) {
            nameBytes = nameBytes.left(13); // Truncate if too long
        }
        while (nameBytes.size() < 13) {
            nameBytes.append('\0'); // Pad with nulls
        }
        sectionData.append(nameBytes);
        
        // Write item data (15 bytes) - keep original stats
        QByteArray originalData = getOriginalItemData(item.itemId);
        sectionData.append(originalData);
    }
    
    return sectionData;
}

QByteArray KernelBinParser::getOriginalItemData(quint16 itemId)
{
    // Find original item data from the raw KERNEL.BIN data
    if (ITEM_SECTION_INDEX >= m_sectionOffsets.size()) {
        return QByteArray(15, '\0'); // Return empty data if section not found
    }
    
    quint32 itemSectionOffset = m_sectionOffsets[ITEM_SECTION_INDEX];
    quint32 itemOffset = itemSectionOffset + (itemId * 28) + 13; // Skip name, start at data
    
    if (itemOffset + 15 > m_rawData.size()) {
        return QByteArray(15, '\0'); // Return empty data if out of bounds
    }
    
    return m_rawData.mid(itemOffset, 15);
}
