// ============================================================================
// WorldScriptEditor — re-offsetting editor for the FF7 world-map script (wm0.ev).
//
// The world script is a separate VM from field scripts: a fixed 0x7000-byte EV
// file = 4-byte dummy entry + a 255-entry call table (header:u16 + word-offset:u16
// each, offsets relative to 0x400) + packed function bodies from 0x400, every
// opcode and parameter a little-endian u16, each function terminated by RETURN
// (0x203). GOTO / GOTO_IF_FALSE (0x200/0x201) carry an ABSOLUTE word-offset target.
//
// Like FieldScriptEditor, this models the whole code region as a flat instruction
// list with symbolic GOTO targets + symbolic call-table entries, lets callers
// insert/replace/remove opcodes, and re-emits with every offset recomputed:
//   * each function's call-table word-offset,
//   * every GOTO/GOTO_IF_FALSE target,
// all inside the fixed 0x7000 buffer (so GS drops it back into world_us.lgp at the
// same size — no LGP repack). Modelled on Landscaper's evfile.ts / worldscript.ts
// (which it round-trips against).
//
// Correctness gate: parse(ev) then assemble() with NO edits == ev. selfTestRoundTrip
// asserts this; run it over wm0.ev before trusting an insert.
// ============================================================================
#ifndef WORLDSCRIPTEDITOR_H
#define WORLDSCRIPTEDITOR_H

#include <QByteArray>
#include <QString>
#include <QVector>

class WorldScriptEditor
{
public:
    WorldScriptEditor() = default;

    // Parse a wm0.ev file (the raw 0x7000-byte EV, already extracted from the lgp).
    bool parse(const QByteArray &ev, QString &err);
    bool isValid() const { return m_valid; }

    int  instrCount() const { return int(m_instrs.size()); }
    // Raw bytes (LE u16 opcode + params) of instruction idx.
    QByteArray instrBytes(int idx) const;

    // read accessors (idx-checked; return -1 / 0xFFFF on bad idx)
    quint16 opAt(int idx) const;
    int     paramCount(int idx) const;
    quint16 paramAt(int idx, int j) const;
    bool    isGotoAt(int idx) const;
    int     gotoTarget(int idx) const;  // target instruction index, or -1

    // First instruction (>= from) whose opcode == op AND, if params given, whose
    // leading params match. Returns -1 if none.
    int findOpcode(quint16 op, int from = 0) const;

    // The call-table entry (0..255) for a mesh tile, or -1. type: 0=System,1=Model,
    // 2=Mesh. For Mesh, key=(x*36+y)<<4 | id is matched on header low bits as needed;
    // callers usually iterate functions() instead.
    // Convenience: instruction index where the call-table entry `tableIndex` begins.
    int entryStart(int tableIndex) const;
    int entryHeader(int tableIndex) const; // -1 if empty (0xFFFF)

    // --- editing (instruction granularity; indices shift on insert/remove) -----
    // Inserted opcodes must be complete world-script opcodes (LE u16 each). A GOTO
    // may be inserted only via insertGoto (target tracked symbolically).
    bool insertBefore(int idx, const QByteArray &opcodeWords, QString &err);
    bool replaceAt(int idx, const QByteArray &opcodeWords, QString &err);
    bool removeAt(int idx, QString &err);
    // Insert a GOTO/GOTO_IF_FALSE before idx, targeting instruction targetIdx.
    bool insertGoto(int idx, bool ifFalse, int targetIdx, QString &err);

    // Re-emit the whole 0x7000 EV with offsets/targets recomputed.
    QByteArray assemble(QString &err) const;

    static bool selfTestRoundTrip(const QByteArray &ev, QString &err);

private:
    struct Instr {
        quint16          op;
        QVector<quint16> params;  // code params (LE u16 each)
        bool             isGoto;  // GOTO/GOTO_IF_FALSE -> last param is the target
        int              target;  // symbolic target instruction index (or -1)
    };
    struct Entry {
        quint16 header;     // 0xFFFF = empty
        quint16 rawOffset;  // original word-offset
        int     idx;        // resolved instruction index, or -1 (preserve raw)
    };

    static int opCodeParams(quint16 op);  // -1 if unknown

    // map a byte position in the code region to an instruction index (or -1)
    int byteToIndex(int bytePos) const;

    bool m_valid = false;
    int  m_codeStart = 0x400;
    int  m_codeEndByte = 0;       // first byte past the last instruction
    QByteArray m_ev;              // original (for the trailing pad/tail + size)
    QVector<Entry>  m_entries;    // 256 call-table entries
    QVector<Instr>  m_instrs;     // flat code
    QVector<int>    m_instrByte;  // original byte pos per instruction (for byteToIndex)
};

#endif // WORLDSCRIPTEDITOR_H
