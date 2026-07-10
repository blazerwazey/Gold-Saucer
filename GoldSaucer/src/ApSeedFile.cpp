#include "ApSeedFile.h"

#include <QFile>
#include <QtEndian>
#include <zlib.h>

namespace {

// Inflate a raw DEFLATE stream (zip compression method 8).
QByteArray inflateRaw(const QByteArray& comp, quint32 uncompSize)
{
    QByteArray out;
    out.resize(int(uncompSize));
    z_stream strm{};
    strm.next_in   = reinterpret_cast<Bytef*>(const_cast<char*>(comp.constData()));
    strm.avail_in  = uInt(comp.size());
    strm.next_out  = reinterpret_cast<Bytef*>(out.data());
    strm.avail_out = uInt(out.size());
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return QByteArray();
    const int rc = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    if (rc != Z_STREAM_END)
        return QByteArray();
    out.truncate(int(uncompSize) - int(strm.avail_out));
    return out;
}

quint16 rdU16(const QByteArray& d, int off) { return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(d.constData() + off)); }
quint32 rdU32(const QByteArray& d, int off) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(d.constData() + off)); }

// Extract one member from a zip by name. Handles stored (0) and deflate (8),
// no zip64 (our containers are tiny). Returns empty on any structural surprise.
QByteArray zipExtract(const QByteArray& zip, const QByteArray& memberName)
{
    // End Of Central Directory: scan back for PK\x05\x06 (comment can follow).
    int eocd = -1;
    const int scanFrom = qMax(0, zip.size() - 0x10016);
    for (int i = zip.size() - 22; i >= scanFrom; --i) {
        if (zip.at(i) == 'P' && zip.at(i + 1) == 'K'
            && zip.at(i + 2) == 0x05 && zip.at(i + 3) == 0x06) { eocd = i; break; }
    }
    if (eocd < 0) return QByteArray();
    const quint16 count  = rdU16(zip, eocd + 10);
    quint32 cdOff        = rdU32(zip, eocd + 16);
    for (quint16 e = 0; e < count; ++e) {
        if (int(cdOff) + 46 > zip.size() || rdU32(zip, int(cdOff)) != 0x02014b50)
            return QByteArray();
        const quint16 method   = rdU16(zip, int(cdOff) + 10);
        const quint32 compSize = rdU32(zip, int(cdOff) + 20);
        const quint32 uncSize  = rdU32(zip, int(cdOff) + 24);
        const quint16 nameLen  = rdU16(zip, int(cdOff) + 28);
        const quint16 extraLen = rdU16(zip, int(cdOff) + 30);
        const quint16 cmntLen  = rdU16(zip, int(cdOff) + 32);
        const quint32 lhOff    = rdU32(zip, int(cdOff) + 42);
        const QByteArray name  = zip.mid(int(cdOff) + 46, nameLen);
        if (name == memberName) {
            if (int(lhOff) + 30 > zip.size() || rdU32(zip, int(lhOff)) != 0x04034b50)
                return QByteArray();
            const quint16 lhName  = rdU16(zip, int(lhOff) + 26);
            const quint16 lhExtra = rdU16(zip, int(lhOff) + 28);
            const int dataOff = int(lhOff) + 30 + lhName + lhExtra;
            if (dataOff + int(compSize) > zip.size()) return QByteArray();
            const QByteArray comp = zip.mid(dataOff, int(compSize));
            if (method == 0) return comp;                       // stored
            if (method == 8) return inflateRaw(comp, uncSize);  // deflate
            return QByteArray();
        }
        cdOff += 46 + nameLen + extraLen + cmntLen;
    }
    return QByteArray();
}

} // namespace

QByteArray ApSeedFile::readJson(const QString& path)
{
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly))
        return QByteArray();
    const QByteArray raw = f.readAll();
    f.close();
    if (raw.size() >= 2 && raw.at(0) == 'P' && raw.at(1) == 'K') {
        QByteArray payload = zipExtract(raw, QByteArrayLiteral("ff7_seed.json"));
        return payload;   // empty on failure — callers treat as unparseable
    }
    return raw;           // legacy bare JSON
}
