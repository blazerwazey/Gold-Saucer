#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>
#include "TextReplacementConfig.h"

// FF7 Item Data structure for text editing
struct KernelItem {
    quint8 itemId;
    QString originalName;
    QString newName;
    QString description;
    ItemCategory category;
    bool isRandomized;
    
    KernelItem() : itemId(0), isRandomized(false) {}
};

// FF7 KERNEL.BIN Shop structure
struct KernelShop {
    quint8 shopId;
    QVector<quint16> itemIds;
    
    KernelShop() : shopId(0) {}
};

// FF7 KERNEL.BIN Parser
class KernelBinParser
{
public:
    KernelBinParser();
    ~KernelBinParser();
    
    // Load and parse KERNEL.BIN
    bool load(const QString& filePath);
    bool save(const QString& filePath);
    
    // Shop data access
    QVector<KernelShop> getShops() const;
    void setShops(const QVector<KernelShop>& shops);
    KernelShop getShop(quint8 shopId) const;
    bool hasShop(quint8 shopId) const;
    
    // Text editing access
    bool loadAllItemText();
    bool replaceItemText(quint16 itemId, const QString& newName, ItemCategory category);
    bool replaceItemDescription(quint16 itemId, const QString& newDescription);
    bool saveAllItemText();
    QVector<KernelItem> getAllItems() const;
    bool setItemText(quint16 itemId, const QString& name, ItemCategory category);
    
    // Category-specific text replacement
    bool replaceWeaponName(quint16 weaponId, const QString& newName);
    bool replaceArmorName(quint16 armorId, const QString& newName);
    bool replaceAccessoryName(quint16 accessoryId, const QString& newName);
    bool replaceMateriaName(quint16 materiaId, const QString& newName);
    bool replaceKeyItemName(quint16 keyItemId, const QString& newName);
    
    // Utility functions
    bool isValid() const;
    QString getLastError() const;
    int getShopCount() const;
    
private:
    // Parsing functions
    bool parseKernelBin();
    bool parseShopSection();
    bool parseItemSection();
    bool parseTextSections();
    bool extractSection(quint32 sectionOffset, quint32 sectionSize, QByteArray& sectionData);
    
    // Building functions
    QByteArray buildShopSection();
    QByteArray buildItemSection();
    QByteArray buildTextSections();
    bool rebuildKernelBin();
    void updateSectionOffsets(const QByteArray& newItemSection, const QByteArray& newTextSections);
    void rebuildCompleteKernel();
    QByteArray getOriginalItemData(quint16 itemId);
    
    // KERNEL.BIN structure
    struct KernelHeader {
        char magic[9];      // "FF7KERNEL"
        quint8 version;
        quint8 sectionCount;
        // Section offsets follow
    };
    
    static const int SHOP_SECTION_INDEX = 5;  // Shop data is section 5
    static const int ITEM_SECTION_INDEX = 4;  // Item data is section 4
    static const int MAX_SHOPS = 128;         // FF7 supports up to 128 shops
    static const int MAX_ITEMS_PER_SHOP = 64; // Maximum items per shop
    static const int MAX_ITEMS = 128;         // Total items in KERNEL.BIN
    
    QByteArray m_rawData;
    QVector<quint32> m_sectionOffsets;
    QVector<KernelShop> m_shops;
    QVector<KernelItem> m_items;
    QString m_lastError;
    bool m_isValid;
    bool m_isModified;
};
