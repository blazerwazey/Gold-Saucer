#pragma once

#include <QString>
#include <QByteArray>
#include <QMap>
#include "TextReplacementConfig.h"

// FF7 Color codes for text formatting
enum class FF7Color : quint8 {
    Green = 0xD6,    // Materia
    Red = 0xD4,      // Key Items / Special
    Cyan = 0xD7,     // Weapons / Armor
    White = 0xD9,    // Consumables / Reset Color
    Gray = 0xD2      // Special Gray
};

class TextEncoder
{
public:
    TextEncoder();
    
    // Core encoding functions - extends our existing encodeFF7Text logic
    static QByteArray encodeText(const QString& text);
    static QByteArray encodeTextWithColor(const QString& text, FF7Color color);
    static QByteArray encodeColoredItem(const QString& itemName, ItemCategory category);
    
    // Color sequence helpers
    static QByteArray colorSequence(FF7Color color);
    static QByteArray resetColor();
    
    // Item category to color mapping
    static FF7Color getItemColor(ItemCategory category);
    static ItemCategory getItemCategory(quint16 itemId);
    
    // Comprehensive text encoding methods
    static QString ff7ToAscii(const QByteArray& ff7Text);
    static QByteArray asciiToFf7(const QString& ascii);
    static bool isValidFF7Text(const QByteArray& text);
    
    // Category-specific encoding
    static QByteArray encodeItemName(const QString& name, ItemCategory category);
    static QString decodeItemName(const QByteArray& ff7Text, ItemCategory category);
    
    // Text length validation
    static bool isValidItemName(const QString& name, ItemCategory category);
    static int getMaxNameLength(ItemCategory category);
    
    // Archipelago-specific encoding
    static QByteArray encodeForeignItemText(const QString& itemName, const QString& playerName);
    static QByteArray encodeReceivedItemText(const QString& itemName, ItemCategory category);
    
    // Color coding implementation
    static QByteArray encodeColoredText(const QString& text, FF7Color color);
    static QString stripColorCodes(const QString& text);
    static bool hasColorCodes(const QString& text);
    
    // Utility functions
    static QString decodeToReadable(const QByteArray& data);
    static bool isValidColorCode(quint8 byte1, quint8 byte2);

private:
    // Internal encoding logic (based on our proven encodeFF7Text)
    static QByteArray encodeFF7TextInternal(const QString& text);
    
    // Color mapping tables
    static QMap<ItemCategory, FF7Color> s_colorMap;
    static QMap<quint16, ItemCategory> s_itemCategoryMap;
    
    // Helper functions
    static void initializeColorMap();
    static void initializeItemCategoryMap();
    static bool isMapsInitialized();
};
