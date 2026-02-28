#include "FF7String.h"
#include <ff7tk/data/FF7Text.h>

MakouFF7String::MakouFF7String() : _textCacheValid(false)
{
}

MakouFF7String::MakouFF7String(const QByteArray &data) : _data(data), _textCacheValid(false)
{
}

MakouFF7String::MakouFF7String(const QString &text) : _textCacheValid(false)
{
    setText(text);
}

MakouFF7String::MakouFF7String(const QByteArrayView &data) : _data(data.toByteArray()), _textCacheValid(false)
{
}

QByteArray MakouFF7String::data() const
{
    return _data;
}

QString MakouFF7String::text() const
{
    if (!_textCacheValid) {
        _cachedText = FF7Text::toPC(_data);
        _textCacheValid = true;
    }
    return _cachedText;
}

bool MakouFF7String::isEmpty() const
{
    return _data.isEmpty();
}

void MakouFF7String::setText(const QString &text)
{
    _data = FF7Text::toFF7(text);
    invalidateTextCache();
}

void MakouFF7String::setData(const QByteArray &data)
{
    _data = data;
    invalidateTextCache();
}

bool MakouFF7String::operator==(const MakouFF7String &other) const
{
    return _data == other._data;
}

bool MakouFF7String::operator!=(const MakouFF7String &other) const
{
    return _data != other._data;
}

void MakouFF7String::invalidateTextCache()
{
    _textCacheValid = false;
}
