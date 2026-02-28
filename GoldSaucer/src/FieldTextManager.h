#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include "FF7String.h"
#include "ScriptManager.h"

/**
 * High-level field text management system
 * Ported from Makou Reactor's Section1File text functionality
 * Provides safe text manipulation without field corruption
 */
class FieldTextManager
{
public:
    FieldTextManager();
    
    // Core operations (ported from Section1File)
    bool open(const QByteArray &fieldData);
    QByteArray save() const;
    
    // Text management (ported from Section1File)
    void setText(int textID, const MakouFF7String &text);
    bool insertText(int textID, const MakouFF7String &text);
    void deleteText(int textID);
    
    // Text access
    const MakouFF7String &text(int textID) const;
    const QList<MakouFF7String> &texts() const;
    int textCount() const;
    
    // Text ID management (ported from Section1File)
    void shiftTextIds(int fromTextId, int shiftAmount);
    
    // Validation
    bool isOpen() const;
    bool isModified() const;
    
    // Field information
    quint16 version() const;
    quint16 scale() const;
    const QString &author() const;

private:
    // Field data (parsed from Section1File)
    QList<MakouFF7String> _texts;
    QList<ScriptManager> _grpScripts;
    
    // Field metadata
    quint16 _version;
    quint16 _scale;
    QString _author;
    
    // Internal state
    bool _isOpen;
    bool _isModified;
    
    // Field structure information (needed for reconstruction)
    mutable QByteArray _originalFieldData;
    quint32 _stringOffset;
    quint16 _dialogCount;
    quint32 _scriptStart;
    quint32 _scriptEnd;
    
    // Parsing methods (ported from Section1File::open)
    bool parseFieldHeader(const QByteArray &data);
    bool parseScripts(const QByteArray &data);
    bool parseTexts(const QByteArray &data);
    
    // Reconstruction methods (ported from Section1File::save)
    QByteArray reconstructFieldData() const;
    bool reconstructTextsInPlace(QByteArray& fieldData) const;
    QByteArray reconstructScripts() const;
    QByteArray reconstructTexts() const;
    QByteArray reconstructHeader() const;
    
    // Utility methods
    void setModified(bool modified = true);
    quint32 calculateTextOffset(int textId) const;
};
