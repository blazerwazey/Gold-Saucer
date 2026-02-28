#include "TextEncoder.h"
#include <QDebug>
#include <ff7tk/data/FF7Text.h>

// Static member initialization
QMap<ItemCategory, FF7Color> TextEncoder::s_colorMap;
QMap<quint16, ItemCategory> TextEncoder::s_itemCategoryMap;

TextEncoder::TextEncoder()
{
    // Initialize static maps if not already done
    if (!isMapsInitialized()) {
        initializeColorMap();
        initializeItemCategoryMap();
    }
}

QByteArray TextEncoder::encodeText(const QString& text)
{
    return encodeFF7TextInternal(text);
}

QByteArray TextEncoder::encodeTextWithColor(const QString& text, FF7Color color)
{
    QByteArray result;
    result.append(colorSequence(color));
    result.append(encodeFF7TextInternal(text));
    result.append(resetColor());
    return result;
}

QByteArray TextEncoder::encodeColoredItem(const QString& itemName, ItemCategory category)
{
    FF7Color color = getItemColor(category);
    return encodeTextWithColor(itemName, color);
}

QByteArray TextEncoder::colorSequence(FF7Color color)
{
    QByteArray sequence;
    sequence.append(static_cast<char>(0xFE));
    sequence.append(static_cast<char>(static_cast<quint8>(color)));
    return sequence;
}

QByteArray TextEncoder::resetColor()
{
    return colorSequence(FF7Color::White);
}

FF7Color TextEncoder::getItemColor(ItemCategory category)
{
    if (!isMapsInitialized()) {
        initializeColorMap();
    }
    
    return s_colorMap.value(category, FF7Color::White);
}

ItemCategory TextEncoder::getItemCategory(quint16 itemId)
{
    if (!isMapsInitialized()) {
        initializeItemCategoryMap();
    }
    
    return s_itemCategoryMap.value(itemId, ItemCategory::Consumable);
}

QByteArray TextEncoder::encodeForeignItemText(const QString& itemName, const QString& playerName)
{
    // Format: "Sent {Red}Hookshot{White} to {Green}Player 2{White}!"
    QByteArray result;
    
    // "Sent "
    result.append(encodeFF7TextInternal("Sent "));
    
    // {Red}itemName{White}
    result.append(colorSequence(FF7Color::Red));
    result.append(encodeFF7TextInternal(itemName));
    result.append(resetColor());
    
    // " to "
    result.append(encodeFF7TextInternal(" to "));
    
    // {Green}playerName{White}
    result.append(colorSequence(FF7Color::Green));
    result.append(encodeFF7TextInternal(playerName));
    result.append(resetColor());
    
    // "!"
    result.append(encodeFF7TextInternal("!"));
    
    return result;
}

QByteArray TextEncoder::encodeReceivedItemText(const QString& itemName, ItemCategory category)
{
    // Format: "Received {Color}Item Name{White}!"
    QByteArray result;
    
    // "Received "
    result.append(encodeFF7TextInternal("Received "));
    
    // {Color}itemName{White}
    result.append(encodeColoredItem(itemName, category));
    
    // "!"
    result.append(encodeFF7TextInternal("!"));
    
    return result;
}

QString TextEncoder::decodeToReadable(const QByteArray& data) {
    return FF7Text::toPC(data);
}

bool TextEncoder::isValidColorCode(quint8 byte1, quint8 byte2)
{
    return byte1 == 0xFE && (
        byte2 == static_cast<quint8>(FF7Color::Green) ||
        byte2 == static_cast<quint8>(FF7Color::Red) ||
        byte2 == static_cast<quint8>(FF7Color::Cyan) ||
        byte2 == static_cast<quint8>(FF7Color::White) ||
        byte2 == static_cast<quint8>(FF7Color::Gray)
    );
}

QByteArray TextEncoder::encodeFF7TextInternal(const QString& text) {
    return FF7Text::toFF7(text);
}

void TextEncoder::initializeColorMap()
{
    s_colorMap.clear();
    s_colorMap[ItemCategory::Materia] = FF7Color::Green;
    s_colorMap[ItemCategory::KeyItem] = FF7Color::Red;
    s_colorMap[ItemCategory::Weapon] = FF7Color::Cyan;
    s_colorMap[ItemCategory::Armor] = FF7Color::Cyan;
    s_colorMap[ItemCategory::Accessory] = FF7Color::Cyan;
    s_colorMap[ItemCategory::Consumable] = FF7Color::White;
    s_colorMap[ItemCategory::Special] = FF7Color::Gray;
}

void TextEncoder::initializeItemCategoryMap()
{
    s_itemCategoryMap.clear();
    
    // Materia (0x00 - 0x5A)
    for (quint16 id = 0x00; id <= 0x5A; id++) {
        s_itemCategoryMap[id] = ItemCategory::Materia;
    }
    
    // Key Items (0x80 - 0x9F)
    for (quint16 id = 0x80; id <= 0x9F; id++) {
        s_itemCategoryMap[id] = ItemCategory::KeyItem;
    }
    
    // Weapons (0x00 - 0x7F in weapon category)
    // This is simplified - in practice we'd use FF7TK item definitions
    for (quint16 id = 0x01; id <= 0x7F; id++) {
        if (id >= 0x01 && id <= 0x1F) {
            s_itemCategoryMap[id] = ItemCategory::Weapon;
        } else if (id >= 0x20 && id <= 0x3F) {
            s_itemCategoryMap[id] = ItemCategory::Armor;
        } else if (id >= 0x40 && id <= 0x5F) {
            s_itemCategoryMap[id] = ItemCategory::Accessory;
        } else {
            s_itemCategoryMap[id] = ItemCategory::Consumable;
        }
    }
}

bool TextEncoder::isMapsInitialized()
{
    return !s_colorMap.isEmpty() && !s_itemCategoryMap.isEmpty();
}

QString TextEncoder::ff7ToAscii(const QByteArray& ff7Text)
{
    return decodeToReadable(ff7Text);
}

QByteArray TextEncoder::asciiToFf7(const QString& ascii)
{
    return encodeFF7TextInternal(ascii);
}

bool TextEncoder::isValidFF7Text(const QByteArray& text)
{
    // Basic validation - check for null-terminated FF7 text
    if (text.isEmpty()) {
        return false;
    }
    
    // Check for valid FF7 characters (simplified validation)
    for (char byte : text) {
        quint8 value = static_cast<quint8>(byte);
        // FF7 text uses specific character ranges
        if (value < 0x20 || value > 0xFF) {
            return false;
        }
    }
    
    return true;
}

QByteArray TextEncoder::encodeItemName(const QString& name, ItemCategory category)
{
    return encodeFF7TextInternal(name);
}

QString TextEncoder::decodeItemName(const QByteArray& ff7Text, ItemCategory category)
{
    return decodeToReadable(ff7Text);
}

bool TextEncoder::isValidItemName(const QString& name, ItemCategory category)
{
    if (name.isEmpty()) {
        return false;
    }
    
    // Check for valid FF7 characters
    for (const QChar& c : name) {
        if (!c.isPrint() && c != ' ') {
            return false;
        }
    }
    
    // Check length limit (considering color codes)
    QString cleanName = stripColorCodes(name);
    int maxLength = getMaxNameLength(category);
    if (cleanName.length() > maxLength) {
        return false;
    }
    
    return true;
}

int TextEncoder::getMaxNameLength(ItemCategory category)
{
    // FF7 item names are limited to 13 characters
    return 13;
}

QByteArray TextEncoder::encodeColoredText(const QString& text, FF7Color color)
{
    QByteArray result;
    result.append(colorSequence(color));
    result.append(encodeFF7TextInternal(text));
    result.append(resetColor());
    return result;
}

QString TextEncoder::stripColorCodes(const QString& text)
{
    QString result = text;
    // Remove FF7 color codes (0xFE + color byte)
    result.remove(QChar(0xFE));
    return result;
}

bool TextEncoder::hasColorCodes(const QString& text)
{
    return text.contains(QChar(0xFE));
}
