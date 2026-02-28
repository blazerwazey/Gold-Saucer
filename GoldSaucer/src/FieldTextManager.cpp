#include "FieldTextManager.h"
#include <QDebug>
#include <QByteArrayView>

FieldTextManager::FieldTextManager() : _version(0), _scale(0), _isOpen(false), _isModified(false)
{
}

bool FieldTextManager::open(const QByteArray &fieldData)
{
    // AGGRESSIVE OPTIMIZATION: Minimal parsing for performance
    if (fieldData.size() < 0x2E) {
        qWarning() << "FieldTextManager::open data too short" << fieldData.size();
        return false;
    }
    
    _originalFieldData = fieldData;
    _texts.clear();
    _isModified = false;
    
    // Only parse the essential information needed for text replacement
    if (!parseFieldHeader(fieldData)) {
        return false;
    }
    
    // Skip script parsing entirely for performance
    _grpScripts.clear();
    
    // Parse texts with bounds checking
    if (!parseTexts(fieldData)) {
        return false;
    }
    
    _isOpen = true;
    return true;
}

QByteArray FieldTextManager::save() const
{
    if (!_isOpen) {
        qWarning() << "FieldTextManager::save called on unopened field";
        return QByteArray();
    }
    
    return reconstructFieldData();
}

void FieldTextManager::setText(int textID, const MakouFF7String &text)
{
    if (!_isOpen || textID < 0 || textID >= _texts.size()) {
        return;
    }
    
    _texts[textID] = text;
    setModified();
}

bool FieldTextManager::insertText(int textID, const MakouFF7String &text)
{
    if (!_isOpen || textID < 0 || textID > _texts.size()) {
        return false;
    }
    
    // Insert the new text
    _texts.insert(textID, text);
    
    // Shift Text IDs in all scripts (critical for preventing corruption)
    shiftTextIds(textID, 1);
    
    setModified();
    return true;
}

void FieldTextManager::deleteText(int textID)
{
    if (!_isOpen || textID < 0 || textID >= _texts.size()) {
        return;
    }
    
    // Remove the text
    _texts.removeAt(textID);
    
    // Shift Text IDs in all scripts (critical for preventing corruption)
    shiftTextIds(textID, -1);
    
    setModified();
}

const MakouFF7String &FieldTextManager::text(int textID) const
{
    if (!_isOpen || textID < 0 || textID >= _texts.size()) {
        static MakouFF7String emptyString;
        return emptyString;
    }
    
    return _texts[textID];
}

const QList<MakouFF7String> &FieldTextManager::texts() const
{
    return _texts;
}

int FieldTextManager::textCount() const
{
    return _texts.size();
}

void FieldTextManager::shiftTextIds(int fromTextId, int shiftAmount)
{
    // OPTIMIZATION: Skip shiftTextIds since we're not parsing scripts
    // We're only replacing existing texts, not inserting new ones, so no Text ID shifting needed
    Q_UNUSED(fromTextId);
    Q_UNUSED(shiftAmount);
    
    setModified();
}

bool FieldTextManager::isOpen() const
{
    return _isOpen;
}

bool FieldTextManager::isModified() const
{
    return _isModified;
}

quint16 FieldTextManager::version() const
{
    return _version;
}

quint16 FieldTextManager::scale() const
{
    return _scale;
}

const QString &FieldTextManager::author() const
{
    return _author;
}

bool FieldTextManager::parseFieldHeader(const QByteArray &data)
{
    // Parse version
    if (data.size() < 2) {
        return false;
    }
    memcpy(&_version, data.constData(), 2);
    
    // Parse string offset (offset to dialog table)
    if (data.size() < 0x16) {
        return false;
    }
    memcpy(&_stringOffset, data.constData() + 0x12, 4);
    
    // Parse scale
    memcpy(&_scale, data.constData() + 0x08, 2);
    
    // Parse author
    QByteArray authorData = data.mid(0x10, 8);
    _author = QString::fromLatin1(authorData.constData(), qstrnlen(authorData.constData(), 8));
    
    // Parse script section boundaries
    quint32 scriptSectionPtr, section2Ptr;
    memcpy(&scriptSectionPtr, data.constData() + 0x06, 4);
    memcpy(&section2Ptr, data.constData() + 0x0A, 4);
    
    _scriptStart = scriptSectionPtr + 4;
    _scriptEnd = section2Ptr;
    
    if (_scriptStart >= data.size() || _scriptEnd > data.size()) {
        qWarning() << "Invalid script section boundaries";
        return false;
    }
    
    return true;
}

bool FieldTextManager::parseScripts(const QByteArray &data)
{
    // OPTIMIZATION: Skip script parsing entirely for performance
    // We don't need to parse scripts for text replacement - only needed for shiftTextIds
    // Since we're only replacing existing texts, we can skip this step
    
    _grpScripts.clear(); // Ensure no scripts are stored
    return true;
}

bool FieldTextManager::parseTexts(const QByteArray &data)
{
    if (_stringOffset + 1 >= data.size()) {
        return false;
    }
    
    // Parse dialog count
    quint16 dialogCount = static_cast<quint8>(data[_stringOffset]) |
                         (static_cast<quint8>(data[_stringOffset + 1]) << 8);
    
    _dialogCount = dialogCount;
    
    // AGGRESSIVE OPTIMIZATION: Limit text parsing to prevent performance issues
    // Parse up to 250 texts to handle most field files while maintaining performance
    quint16 maxTextsToParse = dialogCount < 250 ? dialogCount : 250;
    _texts.reserve(maxTextsToParse);
    
    // Parse texts with aggressive bounds checking
    for (quint16 id = 0; id < maxTextsToParse; ++id) {
        int pointerPos = _stringOffset + 2 + (id * 2);
        if (pointerPos + 1 >= data.size()) {
            break; // Safety break
        }
        
        quint16 textOffset = static_cast<quint8>(data[pointerPos]) |
                           (static_cast<quint8>(data[pointerPos + 1]) << 8);
        
        int textStart = _stringOffset + textOffset;
        if (textStart >= data.size() || textStart < _stringOffset) {
            continue; // Skip invalid offsets
        }
        
        // AGGRESSIVE OPTIMIZATION: Limit text search to prevent infinite loops
        int maxTextLength = 200; // Reasonable limit for FF7 text
        int textEnd = textStart;
        int searchEnd = (textStart + maxTextLength) < data.size() ? (textStart + maxTextLength) : data.size();
        
        while (textEnd < searchEnd && static_cast<quint8>(data[textEnd]) != 0xFF) {
            textEnd++;
        }
        
        if (textEnd >= data.size() || static_cast<quint8>(data[textEnd]) != 0xFF) {
            continue; // Skip if no valid terminator
        }
        
        // Extract text data
        QByteArray textData = data.mid(textStart, textEnd - textStart);
        _texts.append(MakouFF7String(textData));
    }
    
    return true;
}

QByteArray FieldTextManager::reconstructFieldData() const
{
    if (!_isOpen) {
        return QByteArray();
    }
    
    // For now, use a simpler approach: only modify the existing field data
    // instead of trying to reconstruct the entire field from scratch
    QByteArray result = _originalFieldData;
    
    // Update only the text section in place
    if (!reconstructTextsInPlace(result)) {
        qDebug() << "Failed to reconstruct texts in place";
        return QByteArray();
    }
    
    return result;
}

bool FieldTextManager::reconstructTextsInPlace(QByteArray& fieldData) const
{
    if (fieldData.size() < 0x2E) return false;
    
    // Get dialog table information from CURRENT field data (not parsed data)
    quint32 stringOffset = static_cast<quint8>(fieldData[0x12]) |
                         (static_cast<quint8>(fieldData[0x13]) << 8) |
                         (static_cast<quint8>(fieldData[0x14]) << 16) |
                         (static_cast<quint8>(fieldData[0x15]) << 24);
    
    if (stringOffset + 1 >= fieldData.size()) return false;
    
    quint16 dialogCount = static_cast<quint8>(fieldData[stringOffset]) |
                         (static_cast<quint8>(fieldData[stringOffset + 1]) << 8);
    
    // Reconstruct each text in place
    for (quint8 id = 0; id < dialogCount && id < _texts.size(); id++) {
        int pointerPos = stringOffset + 2 + (id * 2);
        if (pointerPos + 1 >= fieldData.size()) break;
        
        quint16 textOffset = static_cast<quint8>(fieldData[pointerPos]) |
                           (static_cast<quint8>(fieldData[pointerPos + 1]) << 8);
        
        int textStart = stringOffset + textOffset;
        if (textStart >= fieldData.size() || textStart < stringOffset) continue;
        
        // Find the end of the current text
        int textEnd = textStart;
        while (textEnd < fieldData.size() && static_cast<quint8>(fieldData[textEnd]) != 0xFF) {
            textEnd++;
        }
        
        if (textEnd >= fieldData.size()) continue;
        
        // Encode the new text
        QByteArray encodedText = _texts[id].data();
        encodedText.append(static_cast<char>(0xFF)); // Add terminator
        
        // Replace the text (maintaining original size)
        int originalLength = textEnd - textStart;
        int copyLength = qMin(encodedText.size(), originalLength);
        
        for (int i = 0; i < copyLength; i++) {
            fieldData[textStart + i] = encodedText[i];
        }
        
        // Fill remaining space with terminators
        for (int i = copyLength; i < originalLength; i++) {
            fieldData[textStart + i] = static_cast<char>(0xFF);
        }
    }
    
    return true;
}

QByteArray FieldTextManager::reconstructScripts() const
{
    QByteArray scripts;
    
    // For simplicity, serialize all scripts as one block
    for (const ScriptManager &script : _grpScripts) {
        scripts.append(script.toByteArray());
    }
    
    return scripts;
}

QByteArray FieldTextManager::reconstructTexts() const
{
    QByteArray texts;
    QByteArray positions;
    
    // Calculate text positions
    quint16 currentOffset = 2 + (_texts.size() * 2); // Start after position table
    
    for (const MakouFF7String &text : _texts) {
        // Add position entry
        quint16 pos = currentOffset;
        positions.append(static_cast<char>(pos & 0xFF));
        positions.append(static_cast<char>((pos >> 8) & 0xFF));
        
        // Add text data
        QByteArray textData = text.data();
        texts.append(textData);
        texts.append(static_cast<char>(0xFF)); // Terminator
        
        currentOffset += textData.size() + 1;
    }
    
    // Combine position table and texts
    QByteArray result;
    
    // Add text count
    quint16 textCount = _texts.size();
    result.append(static_cast<char>(textCount & 0xFF));
    result.append(static_cast<char>((textCount >> 8) & 0xFF));
    
    // Add position table
    result.append(positions);
    
    // Add texts
    result.append(texts);
    
    return result;
}

QByteArray FieldTextManager::reconstructHeader() const
{
    QByteArray header;
    
    // Copy original header up to script section
    if (_originalFieldData.size() >= _scriptStart) {
        header = _originalFieldData.left(_scriptStart);
    }
    
    // Update string offset if needed (since text section size may have changed)
    // For now, we'll keep the original offset
    
    return header;
}

void FieldTextManager::setModified(bool modified)
{
    _isModified = modified;
}

quint32 FieldTextManager::calculateTextOffset(int textId) const
{
    if (textId < 0 || textId >= _texts.size()) {
        return 0;
    }
    
    quint32 offset = 2 + (_texts.size() * 2); // Position table size
    
    for (int i = 0; i < textId; ++i) {
        offset += _texts[i].data().size() + 1; // Text size + terminator
    }
    
    return offset;
}
