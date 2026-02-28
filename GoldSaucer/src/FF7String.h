#pragma once

#include <QByteArray>
#include <QString>

/**
 * High-level FF7 text encoding/decoding class
 * Ported from Makou Reactor's FF7String interface
 * Uses ff7tk for actual encoding/decoding operations
 */
class MakouFF7String
{
public:
    MakouFF7String();
    explicit MakouFF7String(const QByteArray &data);
    explicit MakouFF7String(const QString &text);
    MakouFF7String(const QByteArrayView &data);
    
    // Accessors
    QByteArray data() const;
    QString text() const;
    bool isEmpty() const;
    
    // Modifiers
    void setText(const QString &text);
    void setData(const QByteArray &data);
    
    // Compatibility with Makou's interface
    bool operator==(const MakouFF7String &other) const;
    bool operator!=(const MakouFF7String &other) const;

private:
    QByteArray _data;
    mutable QString _cachedText; // Cache for text() conversion
    mutable bool _textCacheValid;
    
    void invalidateTextCache();
};
