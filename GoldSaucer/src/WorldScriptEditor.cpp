#include "WorldScriptEditor.h"
#include <QtGlobal>
#include <algorithm>
#include <cstring>

// ----------------------------------------------------------------------------
// LE u16 helpers (all world-script units are little-endian u16)
// ----------------------------------------------------------------------------
static inline quint16 rd16(const QByteArray &d, int p) {
    return quint16(quint8(d.at(p))) | (quint16(quint8(d.at(p + 1))) << 8);
}
static inline void ap16(QByteArray &d, quint16 v) {
    d.append(char(v & 0xFF)); d.append(char((v >> 8) & 0xFF));
}

// ----------------------------------------------------------------------------
// opcode code-param table (from Landscaper opcodes.ts). CALL_FN_0..43 = 0x204-0x22F.
// Only GOTO/GOTO_IF_FALSE (0x200/0x201) and PUSH_* constants carry a code param.
// ----------------------------------------------------------------------------
int WorldScriptEditor::opCodeParams(quint16 op)
{
    static const struct { quint16 code; quint8 cp; } kWsOps[] = {
        {0x0,0}, {0x15,0}, {0x17,0}, {0x18,0}, {0x19,0}, {0x1b,0}, {0x30,0}, {0x40,0}, {0x41,0},
        {0x50,0}, {0x51,0}, {0x60,0}, {0x61,0}, {0x62,0}, {0x63,0}, {0x70,0}, {0x80,0}, {0xa0,0},
        {0xb0,0}, {0xc0,0}, {0xe0,0}, {0x100,0}, {0x110,1}, {0x114,1}, {0x117,1}, {0x118,1},
        {0x119,1}, {0x11b,1}, {0x11c,1}, {0x11d,1}, {0x11f,1}, {0x200,1}, {0x201,1}, {0x203,0},
        {0x204,0}, {0x300,0}, {0x302,0}, {0x303,0}, {0x304,0}, {0x305,0}, {0x306,0}, {0x307,0},
        {0x308,0}, {0x309,0}, {0x30a,0}, {0x30b,0}, {0x30c,0}, {0x30d,0}, {0x30e,0}, {0x310,0},
        {0x311,0}, {0x312,0}, {0x313,0}, {0x314,0}, {0x315,0}, {0x316,0}, {0x317,0}, {0x318,0},
        {0x319,0}, {0x31b,0}, {0x31c,0}, {0x31d,0}, {0x31f,0}, {0x320,0}, {0x321,0}, {0x324,0},
        {0x325,0}, {0x326,0}, {0x327,0}, {0x328,0}, {0x329,0}, {0x32a,0}, {0x32b,0}, {0x32c,0},
        {0x32d,0}, {0x32e,0}, {0x32f,0}, {0x330,0}, {0x331,0}, {0x332,0}, {0x333,0}, {0x334,0},
        {0x336,0}, {0x339,0}, {0x33a,0}, {0x33b,0}, {0x33c,0}, {0x33d,0}, {0x33e,0}, {0x347,0},
        {0x348,0}, {0x349,0}, {0x34a,0}, {0x34b,0}, {0x34c,0}, {0x34d,0}, {0x34e,0}, {0x34f,0},
        {0x350,0}, {0x351,0}, {0x352,0}, {0x353,0}, {0x354,0}, {0x355,0},
    };
    static int table[0x360];
    static bool init = false;
    if (!init) {
        for (int &t : table) t = -1;
        for (const auto &e : kWsOps) table[e.code] = e.cp;
        init = true;
    }
    if (op >= 0x204 && op <= 0x22F) return 0;  // CALL_FN_0..43
    if (op < 0x360) return table[op];
    return -1;
}

static inline bool isGotoOp(quint16 op) { return op == 0x200 || op == 0x201; }

// ----------------------------------------------------------------------------
int WorldScriptEditor::byteToIndex(int bytePos) const
{
    // m_instrByte is sorted ascending
    auto it = std::lower_bound(m_instrByte.begin(), m_instrByte.end(), bytePos);
    if (it != m_instrByte.end() && *it == bytePos)
        return int(it - m_instrByte.begin());
    return -1;
}

// ----------------------------------------------------------------------------
// parse
// ----------------------------------------------------------------------------
bool WorldScriptEditor::parse(const QByteArray &ev, QString &err)
{
    m_valid = false;
    m_ev = ev;
    const int sz = ev.size();
    if (sz < m_codeStart) { err = "EV too small"; return false; }

    // 256 call-table entries (entry 0 = dummy), each header:u16 + word-offset:u16
    m_entries.resize(256);
    for (int i = 0; i < 256; ++i) {
        m_entries[i].header    = rd16(ev, i * 4);
        m_entries[i].rawOffset = rd16(ev, i * 4 + 2);
        m_entries[i].idx       = -1;
    }

    // code end = max RETURN-terminated extent over all non-empty functions
    int codeEndByte = m_codeStart;
    for (int i = 0; i < 256; ++i) {
        if (m_entries[i].header == 0xFFFF) continue;
        int p = m_codeStart + int(m_entries[i].rawOffset) * 2;
        int guard = 0;
        while (p + 2 <= sz && guard++ < 200000) {
            quint16 op = rd16(ev, p);
            int cp = opCodeParams(op);
            if (cp < 0) { err = QStringLiteral("unknown opcode 0x%1 @0x%2").arg(op,0,16).arg(p,0,16); return false; }
            p += 2 * (1 + cp);
            if (op == 0x203) break;  // RETURN ends this function body
        }
        if (p > codeEndByte) codeEndByte = p;
    }
    if (codeEndByte > sz) { err = "code overruns file"; return false; }
    m_codeEndByte = codeEndByte;

    // walk the whole code region [0x400, codeEndByte) as a flat instruction stream
    m_instrs.clear();
    m_instrByte.clear();
    int pos = m_codeStart, guard = 0;
    while (pos < codeEndByte && guard++ < 500000) {
        quint16 op = rd16(ev, pos);
        int cp = opCodeParams(op);
        if (cp < 0 || pos + 2 * (1 + cp) > codeEndByte) {
            err = QStringLiteral("code walk desync @0x%1 (op 0x%2)").arg(pos,0,16).arg(op,0,16); return false;
        }
        Instr in; in.op = op; in.isGoto = isGotoOp(op); in.target = -1;
        for (int j = 0; j < cp; ++j) in.params.append(rd16(ev, pos + 2 + 2 * j));
        m_instrByte.append(pos);
        m_instrs.append(in);
        pos += 2 * (1 + cp);
    }
    if (pos != codeEndByte) { err = "code walk did not land on boundary"; return false; }

    // resolve GOTO targets (absolute word-offset -> instruction index)
    for (Instr &in : m_instrs) {
        if (!in.isGoto) continue;
        int tByte = m_codeStart + int(in.params.last()) * 2;
        int tIdx = byteToIndex(tByte);
        if (tIdx < 0) { err = QStringLiteral("GOTO target 0x%1 is not an instruction boundary").arg(tByte,0,16); return false; }
        in.target = tIdx;
    }

    // resolve call-table entry offsets to instruction indices (preserve unresolvable)
    for (Entry &e : m_entries) {
        if (e.header == 0xFFFF) continue;
        int b = m_codeStart + int(e.rawOffset) * 2;
        e.idx = byteToIndex(b);   // -1 if not a boundary -> rawOffset preserved
    }

    m_valid = true;
    return true;
}

// ----------------------------------------------------------------------------
QByteArray WorldScriptEditor::instrBytes(int idx) const
{
    if (idx < 0 || idx >= m_instrs.size()) return QByteArray();
    QByteArray b; ap16(b, m_instrs[idx].op);
    for (quint16 p : m_instrs[idx].params) ap16(b, p);
    return b;
}

int WorldScriptEditor::findOpcode(quint16 op, int from) const
{
    for (int i = qMax(0, from); i < m_instrs.size(); ++i)
        if (m_instrs[i].op == op) return i;
    return -1;
}

quint16 WorldScriptEditor::opAt(int idx) const
{ return (idx >= 0 && idx < m_instrs.size()) ? m_instrs[idx].op : quint16(0xFFFF); }

int WorldScriptEditor::paramCount(int idx) const
{ return (idx >= 0 && idx < m_instrs.size()) ? m_instrs[idx].params.size() : 0; }

quint16 WorldScriptEditor::paramAt(int idx, int j) const
{ return (idx >= 0 && idx < m_instrs.size() && j >= 0 && j < m_instrs[idx].params.size()) ? m_instrs[idx].params[j] : quint16(0); }

bool WorldScriptEditor::isGotoAt(int idx) const
{ return (idx >= 0 && idx < m_instrs.size()) && m_instrs[idx].isGoto; }

int WorldScriptEditor::gotoTarget(int idx) const
{ return (idx >= 0 && idx < m_instrs.size() && m_instrs[idx].isGoto) ? m_instrs[idx].target : -1; }

int WorldScriptEditor::entryStart(int tableIndex) const
{
    if (tableIndex < 0 || tableIndex >= m_entries.size()) return -1;
    return m_entries[tableIndex].idx;
}
int WorldScriptEditor::entryHeader(int tableIndex) const
{
    if (tableIndex < 0 || tableIndex >= m_entries.size()) return -1;
    quint16 h = m_entries[tableIndex].header;
    return h == 0xFFFF ? -1 : int(h);
}

// ----------------------------------------------------------------------------
// editing
// ----------------------------------------------------------------------------
bool WorldScriptEditor::insertBefore(int idx, const QByteArray &opcodeWords, QString &err)
{
    if (!m_valid) { err = "not parsed"; return false; }
    if (idx < 0 || idx > m_instrs.size()) { err = "insert index out of range"; return false; }
    if (opcodeWords.size() % 2 != 0) { err = "opcode bytes not u16-aligned"; return false; }
    QVector<Instr> add;
    int p = 0;
    while (p < opcodeWords.size()) {
        quint16 op = rd16(opcodeWords, p);
        int cp = opCodeParams(op);
        if (cp < 0) { err = QStringLiteral("invalid inserted opcode 0x%1").arg(op,0,16); return false; }
        if (isGotoOp(op)) { err = "use insertGoto for GOTO opcodes"; return false; }
        if (p + 2 * (1 + cp) > opcodeWords.size()) { err = "truncated inserted opcode"; return false; }
        Instr in; in.op = op; in.isGoto = false; in.target = -1;
        for (int j = 0; j < cp; ++j) in.params.append(rd16(opcodeWords, p + 2 + 2 * j));
        add.append(in);
        p += 2 * (1 + cp);
    }
    const int n = add.size();
    for (Instr &in : m_instrs) if (in.isGoto && in.target >= idx) in.target += n;
    for (Entry &e : m_entries) if (e.idx >= idx) e.idx += n;
    for (int k = 0; k < n; ++k) m_instrs.insert(idx + k, add[k]);
    return true;
}

bool WorldScriptEditor::insertGoto(int idx, bool ifFalse, int targetIdx, QString &err)
{
    if (!m_valid) { err = "not parsed"; return false; }
    if (idx < 0 || idx > m_instrs.size()) { err = "insert index out of range"; return false; }
    if (targetIdx < 0 || targetIdx > m_instrs.size()) { err = "goto target out of range"; return false; }
    Instr in; in.op = ifFalse ? 0x201 : 0x200; in.isGoto = true; in.params.append(0); in.target = targetIdx;
    for (Instr &x : m_instrs) if (x.isGoto && x.target >= idx) x.target += 1;
    for (Entry &e : m_entries) if (e.idx >= idx) e.idx += 1;
    if (in.target >= idx) in.target += 1;   // our own target shifts too
    m_instrs.insert(idx, in);
    return true;
}

bool WorldScriptEditor::replaceAt(int idx, const QByteArray &opcodeWords, QString &err)
{
    if (!m_valid) { err = "not parsed"; return false; }
    if (idx < 0 || idx >= m_instrs.size()) { err = "replace index out of range"; return false; }
    if (opcodeWords.size() < 2) { err = "empty replacement"; return false; }
    quint16 op = rd16(opcodeWords, 0);
    int cp = opCodeParams(op);
    if (cp < 0 || 2 * (1 + cp) != opcodeWords.size()) { err = "replacement must be exactly one opcode"; return false; }
    if (isGotoOp(op)) { err = "cannot replace with a GOTO (use insertGoto/removeAt)"; return false; }
    Instr &in = m_instrs[idx];
    in.op = op; in.isGoto = false; in.target = -1; in.params.clear();
    for (int j = 0; j < cp; ++j) in.params.append(rd16(opcodeWords, 2 + 2 * j));
    return true;
}

bool WorldScriptEditor::removeAt(int idx, QString &err)
{
    if (!m_valid) { err = "not parsed"; return false; }
    if (idx < 0 || idx >= m_instrs.size()) { err = "remove index out of range"; return false; }
    for (const Instr &in : m_instrs) if (in.isGoto && in.target == idx) { err = "cannot remove a GOTO target"; return false; }
    for (const Entry &e : m_entries) if (e.idx == idx) { err = "cannot remove a function's first instruction"; return false; }
    m_instrs.remove(idx);
    for (Instr &in : m_instrs) if (in.isGoto && in.target > idx) in.target -= 1;
    for (Entry &e : m_entries) if (e.idx > idx) e.idx -= 1;
    return true;
}

// ----------------------------------------------------------------------------
// assemble
// ----------------------------------------------------------------------------
QByteArray WorldScriptEditor::assemble(QString &err) const
{
    if (!m_valid) { err = "not parsed"; return QByteArray(); }
    const int N = m_instrs.size();

    // word-offset of each instruction (word 0 == 0x400)
    QVector<int> w(N + 1, 0);
    for (int i = 0; i < N; ++i) w[i + 1] = w[i] + 1 + m_instrs[i].params.size();

    // code bytes, with GOTO targets recomputed
    QByteArray code;
    for (int i = 0; i < N; ++i) {
        const Instr &in = m_instrs[i];
        ap16(code, in.op);
        if (in.isGoto) {
            int t = (in.target == N) ? w[N] : w[in.target];
            if (t > 0xFFFF) { err = "GOTO target word-offset overflow"; return QByteArray(); }
            ap16(code, quint16(t));   // single target param
        } else {
            for (quint16 p : in.params) ap16(code, p);
        }
    }

    // call table
    QByteArray table; table.reserve(0x400);
    for (const Entry &e : m_entries) {
        ap16(table, e.header);
        int off = (e.idx >= 0) ? ((e.idx == N) ? w[N] : w[e.idx]) : int(e.rawOffset);
        if (off > 0xFFFF) { err = "function offset overflow"; return QByteArray(); }
        ap16(table, quint16(off));
    }
    if (table.size() != 0x400) { err = "call table size mismatch"; return QByteArray(); }

    QByteArray tail = m_ev.mid(m_codeEndByte);   // trailing data + zero padding, verbatim
    QByteArray out = table + code + tail;
    if (out.size() > m_ev.size()) {
        // inserts grow the code into the trailing padding — drop that many trailing
        // bytes, but only if they are zero padding (never lose real data).
        const int excess = out.size() - m_ev.size();
        for (int i = out.size() - excess; i < out.size(); ++i)
            if (out.at(i) != 0) { err = QStringLiteral("EV overflow: %1 bytes past 0x%2 of non-pad data").arg(excess).arg(m_ev.size(),0,16); return QByteArray(); }
        out.truncate(m_ev.size());
    }
    if (out.size() < m_ev.size()) out.append(QByteArray(m_ev.size() - out.size(), 0));
    return out;
}

// ----------------------------------------------------------------------------
bool WorldScriptEditor::selfTestRoundTrip(const QByteArray &ev, QString &err)
{
    WorldScriptEditor w;
    if (!w.parse(ev, err)) return false;
    QByteArray out = w.assemble(err);
    if (out.isEmpty()) return false;
    if (out != ev) {
        err = QStringLiteral("round-trip mismatch (in=%1 out=%2)").arg(ev.size()).arg(out.size());
        int n = qMin(ev.size(), out.size());
        for (int i = 0; i < n; ++i) if (ev.at(i) != out.at(i)) { err += QStringLiteral(" first diff @0x%1").arg(i,0,16); break; }
        return false;
    }
    return true;
}
