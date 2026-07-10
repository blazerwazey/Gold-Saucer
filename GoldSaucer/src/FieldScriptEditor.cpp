#include "FieldScriptEditor.h"
#include <QtGlobal>
#include <QHash>
#include <cstring>

// ----------------------------------------------------------------------------
// little-endian helpers
// ----------------------------------------------------------------------------
static inline quint16 rdU16(const QByteArray &d, int p) {
    return quint16(quint8(d.at(p))) | (quint16(quint8(d.at(p + 1))) << 8);
}
static inline quint32 rdU32(const QByteArray &d, int p) {
    return quint32(quint8(d.at(p)))        | (quint32(quint8(d.at(p + 1))) << 8)
         | (quint32(quint8(d.at(p + 2))) << 16) | (quint32(quint8(d.at(p + 3))) << 24);
}
static inline void wrU16(QByteArray &d, int p, quint16 v) {
    d[p] = char(v & 0xFF); d[p + 1] = char((v >> 8) & 0xFF);
}
static inline void wrU32(QByteArray &d, int p, quint32 v) {
    d[p] = char(v & 0xFF);         d[p + 1] = char((v >> 8) & 0xFF);
    d[p + 2] = char((v >> 16) & 0xFF); d[p + 3] = char((v >> 24) & 0xFF);
}

// ----------------------------------------------------------------------------
// opcode metadata (mirrors FieldPickupRandomizer_ff7tk.cpp's fieldOpcodeLength
// table and Makou Reactor's jump model). Keep in sync with that table.
// ----------------------------------------------------------------------------
int FieldScriptEditor::opcodeLength(const QByteArray &d, int pos)
{
    static const int kOperands[256] = {
        /*00*/  0, 2, 2, 2, 2, 2, 2, 1,  1,14, 5, 5,-1,-1, 1, 0,
        /*10*/  1, 2, 1, 2, 5, 6, 7, 8,  7, 8,-1,-1,-1,-1,-1,-1,
        /*20*/ 10, 1, 4, 2, 2, 8, 1, 1,  0, 0, 1, 1, 4, 6, 1, 9,
        /*30*/  3, 3, 3, 1, 1, 3, 4, 7,  5, 5, 5, 3, 0, 0, 0, 0,
        /*40*/  2, 4, 5, 1,-1, 4,-1, 4,  6, 3, 1, 1,-1, 4,-1, 4,
        /*50*/  9, 5, 3, 1, 1, 2, 6, 6,  4, 4, 4, 6, 7, 9, 7, 0,
        /*60*/  9, 1, 4, 5, 5, 0, 8, 0,  8, 1, 6, 8, 0, 3, 2, 5,
        /*70*/  3, 1, 2, 3, 3, 7, 3, 4,  3, 4, 2, 2, 2, 2, 1, 2,
        /*80*/  3, 4, 3, 3, 3, 3, 4, 3,  4, 3, 4, 3, 4, 3, 4, 3,
        /*90*/  4, 3, 4, 3, 4, 2, 2, 2,  2, 2, 3, 4, 5, 6, 6,10,
        /*a0*/  1, 1, 2, 2, 1,10, 8, 8,  5, 5, 1, 3, 0, 5, 2, 2,
        /*b0*/  4, 4, 3, 2, 5, 5, 1, 3,  4, 3, 2, 4, 4, 3,-1, 1,
        /*c0*/ 10, 7,14,11, 0, 2, 2, 1,  1, 1, 3, 2, 2, 2, 1, 1,
        /*d0*/ 12, 1, 1,15, 9, 9, 3, 3,  2, 0,14, 1, 3, 0, 0,10,
        /*e0*/  3, 3, 2, 2, 2, 4, 4, 4,  6, 9, 9, 4, 4, 7, 7,10,
        /*f0*/  1, 4,13, 1, 1, 1, 1, 3,  1, 0, 2, 1, 1, 5, 2, 0,
    };
    const int fileSize = d.size();
    if (pos < 0 || pos >= fileSize) return -1;
    quint8 op = quint8(d.at(pos));

    if (op == 0x0F) { // SPECIAL: 0x0F + sub + sub-operands
        if (pos + 1 >= fileSize) return -1;
        quint8 sub = quint8(d.at(pos + 1));
        int subOps;
        switch (sub) {
            case 0xF5: subOps = 1; break; case 0xF6: subOps = 4; break;
            case 0xF7: subOps = 2; break; case 0xF8: subOps = 2; break;
            case 0xF9: subOps = 0; break; case 0xFA: subOps = 0; break;
            case 0xFB: subOps = 1; break; case 0xFC: subOps = 1; break;
            case 0xFD: subOps = 2; break; case 0xFE: subOps = 0; break;
            case 0xFF: subOps = 0; break; default: return -1;
        }
        int len = 2 + subOps;
        return (pos + len <= fileSize) ? len : -1;
    }
    if (op == 0x28) { // KAWAI: length in second byte
        if (pos + 1 >= fileSize) return -1;
        int len = quint8(d.at(pos + 1));
        if (len < 2) return -1;
        return (pos + len <= fileSize) ? len : -1;
    }
    int ops = kOperands[op];
    if (ops < 0) return -1;
    int len = 1 + ops;
    return (pos + len <= fileSize) ? len : -1;
}

// JMPF/JMPFL/JMPB/JMPBL, IFUB..IFUWL, Unused1B, IFKEY/ON/OFF, IFPRTYQ/IFMEMBQ
bool FieldScriptEditor::opIsJump(quint8 id)
{
    return (id >= 0x10 && id <= 0x19)   // JMPF..IFUWL
        ||  id == 0x1B                  // Unused1B (long jump)
        || (id >= 0x30 && id <= 0x32)   // IFKEY/IFKEYON/IFKEYOFF
        ||  id == 0xCB || id == 0xCC;   // IFPRTYQ / IFMEMBQ
}
bool FieldScriptEditor::opIsBackJump(quint8 id) { return id == 0x12 || id == 0x13; }
bool FieldScriptEditor::opIsLongJump(quint8 id)
{
    return id == 0x11 || id == 0x13 || id == 0x15 || id == 0x17 || id == 0x19 || id == 0x1B;
}
int FieldScriptEditor::opJumpShift(quint8 id)
{
    switch (id) {
        case 0x10: case 0x11: case 0x1B: return 1; // JMPF/JMPFL/Unused1B
        case 0x12: case 0x13:            return 0; // JMPB/JMPBL
        case 0x14: case 0x15:            return 5; // IFUB/IFUBL
        case 0x16: case 0x17:                       // IFSW/IFSWL
        case 0x18: case 0x19:            return 7; // IFUW/IFUWL
        case 0x30: case 0x31: case 0x32: return 3; // IFKEY family
        case 0xCB: case 0xCC:            return 2; // IFPRTYQ/IFMEMBQ
        default:                         return 0;
    }
}
quint8 FieldScriptEditor::opLongVariant(quint8 shortId)
{
    switch (shortId) {
        case 0x10: return 0x11; case 0x12: return 0x13; case 0x14: return 0x15;
        case 0x16: return 0x17; case 0x18: return 0x19; default: return 0;
    }
}

int FieldScriptEditor::readJump(const Instr &in)
{
    int n = in.bytes.size();
    int raw = in.longJump
        ? (quint8(in.bytes.at(n - 2)) | (quint8(in.bytes.at(n - 1)) << 8))
        :  quint8(in.bytes.at(n - 1));
    return in.backJump ? -raw : raw + in.shift;
}

bool FieldScriptEditor::writeJump(Instr &in, int jump, QString &err, bool &sizeChanged) const
{
    sizeChanged = false;
    if (jump < 0) {
        if (!in.backJump) { err = "forward jump resolved to a negative distance"; return false; }
        int raw = -jump;
        if (in.longJump) {
            if (raw > 0xFFFF) { err = "backward long jump overflow"; return false; }
            int n = in.bytes.size(); in.bytes[n - 2] = char(raw & 0xFF); in.bytes[n - 1] = char((raw >> 8) & 0xFF);
        } else if (raw > 0xFF) {
            quint8 lv = opLongVariant(quint8(in.bytes.at(0)));
            if (!lv) { err = "backward jump overflow, no long variant"; return false; }
            in.bytes[0] = char(lv); in.bytes.append(char(0)); in.longJump = true;
            int n = in.bytes.size(); in.bytes[n - 2] = char(raw & 0xFF); in.bytes[n - 1] = char((raw >> 8) & 0xFF);
            sizeChanged = true;
        } else {
            in.bytes[in.bytes.size() - 1] = char(raw & 0xFF);
        }
        return true;
    }
    // forward
    int raw = jump - in.shift;
    if (raw < 0) { err = "jump target falls inside the source opcode"; return false; }
    if (in.longJump) {
        if (raw > 0xFFFF) { err = "forward long jump overflow"; return false; }
        int n = in.bytes.size(); in.bytes[n - 2] = char(raw & 0xFF); in.bytes[n - 1] = char((raw >> 8) & 0xFF);
    } else if (raw > 0xFF) {
        quint8 lv = opLongVariant(quint8(in.bytes.at(0)));
        if (!lv) { err = "forward jump overflow, no long variant"; return false; }
        in.bytes[0] = char(lv); in.bytes.append(char(0)); in.longJump = true;
        int n = in.bytes.size(); in.bytes[n - 2] = char(raw & 0xFF); in.bytes[n - 1] = char((raw >> 8) & 0xFF);
        sizeChanged = true;
    } else {
        in.bytes[in.bytes.size() - 1] = char(raw & 0xFF);
    }
    return true;
}

// ----------------------------------------------------------------------------
// parse
// ----------------------------------------------------------------------------
bool FieldScriptEditor::parse(const QByteArray &field, QString &err)
{
    m_valid = false;
    m_field = field;
    const int sz = field.size();
    if (sz < 46) { err = "field too small"; return false; }

    quint32 sectionPositions[9];
    std::memcpy(sectionPositions, field.constData() + 6, 9 * 4);
    m_sec0LenPos = int(sectionPositions[0]);
    if (m_sec0LenPos + 4 > sz) { err = "bad section-0 pointer"; return false; }
    m_sec0DataStart = m_sec0LenPos + 4;
    m_sectionLen = int(rdU32(field, m_sec0LenPos));
    if (m_sec0DataStart + m_sectionLen > sz || m_sectionLen < 32) { err = "bad section-0 length"; return false; }

    const int sd = m_sec0DataStart;
    m_nbEntities    = quint8(field.at(sd + 2));
    m_wStringOffset = rdU16(field, sd + 4);
    m_nAkao         = rdU16(field, sd + 6);
    if (m_nbEntities == 0) { err = "no entities"; return false; }

    m_header = field.mid(sd, 32);
    const int namesOff   = sd + 32;
    const int akaoOff     = namesOff + 8 * m_nbEntities;
    m_scriptTableOff      = akaoOff + 4 * m_nAkao;
    const int bcStartAbs  = m_scriptTableOff + 64 * m_nbEntities;
    m_bcStartRel          = bcStartAbs - sd;
    if (bcStartAbs > sd + m_sectionLen) { err = "script table overruns section"; return false; }

    m_names = field.mid(namesOff, 8 * m_nbEntities);
    m_akaoOffsets.resize(m_nAkao);
    for (int i = 0; i < m_nAkao; ++i) m_akaoOffsets[i] = rdU32(field, akaoOff + 4 * i);

    // bytecode end = min(string table, first akao block)
    int walkEndRel = m_wStringOffset;
    if (m_nAkao > 0 && int(m_akaoOffsets[0]) < walkEndRel && int(m_akaoOffsets[0]) > m_bcStartRel)
        walkEndRel = int(m_akaoOffsets[0]);
    const int walkEndAbs = sd + walkEndRel;
    if (walkEndAbs > sd + m_sectionLen || walkEndAbs <= bcStartAbs) { err = "bad bytecode bounds"; return false; }
    m_origBcLen = walkEndAbs - bcStartAbs;

    // walk the whole bytecode region as one contiguous opcode stream
    m_instrs.clear();
    QHash<int, int> offToIdx;   // bc-relative offset -> instr index
    int pos = bcStartAbs, guard = 0;
    while (pos < walkEndAbs && guard++ < 1000000) {
        int len = opcodeLength(field, pos);
        if (len <= 0 || pos + len > walkEndAbs) { err = QStringLiteral("opcode walk desync @%1").arg(pos); return false; }
        Instr in;
        in.bytes    = field.mid(pos, len);
        quint8 id   = quint8(in.bytes.at(0));
        in.isJump   = opIsJump(id);
        in.backJump = opIsBackJump(id);
        in.longJump = opIsLongJump(id);
        in.shift    = opJumpShift(id);
        in.target   = -1;
        offToIdx.insert(pos - bcStartAbs, int(m_instrs.size()));
        m_instrs.append(in);
        pos += len;
    }
    if (pos != walkEndAbs) { err = "bytecode walk did not land on boundary"; return false; }
    const int endIdx = int(m_instrs.size());

    // resolve jump targets to instruction indices (target = pos + jump())
    {
        QVector<int> cum(m_instrs.size() + 1, 0);
        for (int i = 0; i < m_instrs.size(); ++i) cum[i + 1] = cum[i] + m_instrs[i].bytes.size();
        for (int i = 0; i < m_instrs.size(); ++i) {
            Instr &in = m_instrs[i];
            if (!in.isJump) continue;
            int tgt = cum[i] + readJump(in);
            int idx;
            if (tgt == m_origBcLen) idx = endIdx;
            else { auto it = offToIdx.constFind(tgt); if (it == offToIdx.constEnd()) { err = QStringLiteral("jump @instr %1 targets non-boundary offset %2").arg(i).arg(tgt); return false; } idx = it.value(); }
            in.target = idx;
        }
    }

    // resolve script starts to instruction indices
    m_scriptStartIdx.resize(m_nbEntities * 32);
    for (int e = 0; e < m_nbEntities; ++e) {
        for (int s = 0; s < 32; ++s) {
            int so = rdU16(field, m_scriptTableOff + 64 * e + 2 * s); // rel sd
            int bcOff = so - m_bcStartRel;
            int idx;
            if (bcOff == m_origBcLen) idx = endIdx;
            else { auto it = offToIdx.constFind(bcOff); if (it == offToIdx.constEnd()) { err = QStringLiteral("entity %1 script %2 offset %3 is not an opcode boundary").arg(e).arg(s).arg(so); return false; } idx = it.value(); }
            m_scriptStartIdx[e * 32 + s] = idx;
        }
    }

    m_tail = field.mid(walkEndAbs, (sd + m_sectionLen) - walkEndAbs);
    m_valid = true;
    return true;
}

int FieldScriptEditor::scriptStart(int entity, int script) const
{
    if (!m_valid || entity < 0 || entity >= m_nbEntities || script < 0 || script >= 32) return -1;
    return m_scriptStartIdx[entity * 32 + script];
}

QByteArray FieldScriptEditor::instrBytes(int idx) const
{
    if (idx < 0 || idx >= m_instrs.size()) return QByteArray();
    return m_instrs[idx].bytes;
}

int FieldScriptEditor::findOpcode(const QByteArray &prefix, int fromInstr) const
{
    if (prefix.isEmpty()) return -1;
    for (int i = qMax(0, fromInstr); i < m_instrs.size(); ++i)
        if (m_instrs[i].bytes.startsWith(prefix)) return i;
    return -1;
}

int FieldScriptEditor::findBytes(const QByteArray &bytes, int fromInstr) const
{
    if (bytes.isEmpty()) return -1;
    for (int i = qMax(0, fromInstr); i < m_instrs.size(); ++i) {
        // does the concatenation of opcodes from i begin with `bytes`?
        int need = bytes.size(), j = i, matched = 0; bool ok = true;
        while (matched < need && j < m_instrs.size()) {
            const QByteArray &b = m_instrs[j].bytes;
            for (int k = 0; k < b.size() && matched < need; ++k, ++matched)
                if (b.at(k) != bytes.at(matched)) { ok = false; break; }
            if (!ok) break;
            ++j;
        }
        if (ok && matched == need) return i;
    }
    return -1;
}

// ----------------------------------------------------------------------------
// editing
// ----------------------------------------------------------------------------
bool FieldScriptEditor::insertBefore(int idx, const QByteArray &opcodeBytes, QString &err)
{
    if (!m_valid) { err = "not parsed"; return false; }
    if (idx < 0 || idx > m_instrs.size()) { err = "insert index out of range"; return false; }
    // decode the inserted opcode(s) into individual instructions
    QVector<Instr> add;
    int p = 0;
    while (p < opcodeBytes.size()) {
        int len = opcodeLength(opcodeBytes, p);
        if (len <= 0) { err = "invalid inserted opcode"; return false; }
        quint8 id = quint8(opcodeBytes.at(p));
        if (opIsJump(id)) { err = "inserting jump opcodes is not supported"; return false; }
        Instr in; in.bytes = opcodeBytes.mid(p, len);
        in.isJump = false; in.backJump = false; in.longJump = false; in.shift = 0; in.target = -1;
        add.append(in);
        p += len;
    }
    const int n = add.size();
    // shift symbolic references at/after the insertion point
    for (Instr &in : m_instrs) if (in.isJump && in.target >= idx) in.target += n;
    for (int &si : m_scriptStartIdx) if (si >= idx) si += n;
    for (int k = 0; k < n; ++k) m_instrs.insert(idx + k, add[k]);
    return true;
}

bool FieldScriptEditor::replaceAt(int idx, const QByteArray &opcodeBytes, QString &err)
{
    if (!m_valid) { err = "not parsed"; return false; }
    if (idx < 0 || idx >= m_instrs.size()) { err = "replace index out of range"; return false; }
    int len = opcodeLength(opcodeBytes, 0);
    if (len <= 0 || len != opcodeBytes.size()) { err = "replacement must be exactly one opcode"; return false; }
    quint8 id = quint8(opcodeBytes.at(0));
    if (opIsJump(id)) { err = "replacing with a jump opcode is not supported"; return false; }
    Instr &in = m_instrs[idx];
    in.bytes = opcodeBytes; in.isJump = false; in.backJump = false; in.longJump = false; in.shift = 0; in.target = -1;
    return true;
}

bool FieldScriptEditor::patchOperands(int idx, int byteOffset, const QByteArray &bytes, QString &err)
{
    if (!m_valid) { err = "not parsed"; return false; }
    if (idx < 0 || idx >= m_instrs.size()) { err = "patch index out of range"; return false; }
    Instr &in = m_instrs[idx];
    if (byteOffset < 1) { err = "patchOperands must not touch the opcode id (offset 0)"; return false; }
    if (byteOffset + bytes.size() > in.bytes.size()) { err = "patch overruns the opcode"; return false; }
    if (in.isJump) {
        // jump operand occupies the trailing 1 (short) or 2 (long) bytes
        int jumpBytes = in.longJump ? 2 : 1;
        int jumpStart = in.bytes.size() - jumpBytes;
        if (byteOffset + bytes.size() > jumpStart) { err = "patch overlaps the jump operand"; return false; }
    }
    for (int i = 0; i < bytes.size(); ++i) in.bytes[byteOffset + i] = bytes.at(i);
    return true;
}

bool FieldScriptEditor::removeAt(int idx, QString &err)
{
    if (!m_valid) { err = "not parsed"; return false; }
    if (idx < 0 || idx >= m_instrs.size()) { err = "remove index out of range"; return false; }
    for (const Instr &in : m_instrs) if (in.isJump && in.target == idx) { err = "cannot remove an instruction that is a jump target"; return false; }
    for (int si : m_scriptStartIdx) if (si == idx) { err = "cannot remove the first instruction of a script"; return false; }
    m_instrs.remove(idx);
    for (Instr &in : m_instrs) if (in.isJump && in.target > idx) in.target -= 1;
    for (int &si : m_scriptStartIdx) if (si > idx) si -= 1;
    return true;
}

// ----------------------------------------------------------------------------
// emit
// ----------------------------------------------------------------------------
QByteArray FieldScriptEditor::assemble(QString &err) const
{
    if (!m_valid) { err = "not parsed"; return QByteArray(); }

    // work on a mutable copy of the instructions so jump rewrites/promotions stick
    QVector<Instr> ins = m_instrs;
    const int N = ins.size();

    // iterate to a fixpoint: recompute every jump from current offsets; if a
    // jump promotes short->long the stream grows, so redo until stable.
    QVector<int> cum(N + 1, 0);
    int guard = 0;
    bool changed = true;
    while (changed && guard++ < 64) {
        changed = false;
        for (int i = 0; i < N; ++i) cum[i + 1] = cum[i] + ins[i].bytes.size();
        for (int i = 0; i < N; ++i) {
            Instr &in = ins[i];
            if (!in.isJump) continue;
            int tgtOff = (in.target == N) ? cum[N] : cum[in.target];
            int J = tgtOff - cum[i];
            bool sizeChanged = false;
            if (!writeJump(in, J, err, sizeChanged)) return QByteArray();
            if (sizeChanged) changed = true;
        }
    }
    if (changed) { err = "jump sizing did not converge"; return QByteArray(); }

    // assemble new bytecode
    QByteArray bc;
    for (const Instr &in : ins) bc.append(in.bytes);
    const int growth = bc.size() - m_origBcLen;

    // new section-0 prologue
    QByteArray header = m_header;
    wrU16(header, 4, quint16(m_wStringOffset + growth));      // string table shifts

    QByteArray akaoTable; akaoTable.reserve(4 * m_nAkao);
    for (int i = 0; i < m_nAkao; ++i) { QByteArray b(4, 0); wrU32(b, 0, m_akaoOffsets[i] + quint32(growth)); akaoTable.append(b); }

    // recompute final per-instruction offsets for the script table
    QVector<int> finalCum(N + 1, 0);
    for (int i = 0; i < N; ++i) finalCum[i + 1] = finalCum[i] + ins[i].bytes.size();

    QByteArray scriptTable(64 * m_nbEntities, 0);
    for (int e = 0; e < m_nbEntities; ++e)
        for (int s = 0; s < 32; ++s) {
            int idx = m_scriptStartIdx[e * 32 + s];
            int off = m_bcStartRel + finalCum[idx];     // idx may be N (end)
            if (off > 0xFFFF) { err = "script offset overflow (>64KB)"; return QByteArray(); }
            wrU16(scriptTable, 64 * e + 2 * s, quint16(off));
        }

    QByteArray sec0 = header + m_names + akaoTable + scriptTable + bc + m_tail;
    if (sec0.size() != m_sectionLen + growth) { err = "section-0 size mismatch after rebuild"; return QByteArray(); }

    // reassemble the field: patch sec0 length prefix + later section pointers
    QByteArray out = m_field;
    wrU32(out, m_sec0LenPos, quint32(m_sectionLen + growth));
    for (int i = 1; i < 9; ++i) {
        quint32 sp = rdU32(out, 6 + 4 * i);
        if (int(sp) > m_sec0LenPos) wrU32(out, 6 + 4 * i, sp + quint32(growth));
    }
    out.replace(m_sec0DataStart, m_sectionLen, sec0);
    return out;
}

// ----------------------------------------------------------------------------
// self-test
// ----------------------------------------------------------------------------
bool FieldScriptEditor::selfTestRoundTrip(const QByteArray &field, QString &err)
{
    FieldScriptEditor ed;
    if (!ed.parse(field, err)) return false;
    QByteArray out = ed.assemble(err);
    if (out.isEmpty()) return false;
    if (out != field) {
        err = QStringLiteral("round-trip mismatch (in=%1 out=%2)").arg(field.size()).arg(out.size());
        // find first differing byte for diagnostics
        int n = qMin(field.size(), out.size());
        for (int i = 0; i < n; ++i) if (field.at(i) != out.at(i)) { err += QStringLiteral(" first diff @%1").arg(i); break; }
        return false;
    }
    return true;
}
