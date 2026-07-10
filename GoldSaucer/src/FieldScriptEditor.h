// ============================================================================
// FieldScriptEditor — re-offsetting field-script editor for FF7 flevel fields.
//
// Unlike the length-preserving byte patches in FieldPickupRandomizer_ff7tk.cpp,
// this editor lets callers INSERT / REPLACE / DELETE opcodes in a field's
// section-0 scripts and then re-emits the whole field with every dependent
// offset recomputed — exactly as Makou Reactor does:
//
//   * the per-entity 32-slot script offset table,
//   * every relative jump (JMPF/JMPB/IFxx…), including short->long promotion
//     when an insertion pushes a jump past 255 bytes,
//   * the section string-table offset and the AKAO offset table,
//   * the section-0 length prefix and the field's section pointer table.
//
// This removes the "can't insert an opcode" constraint that forced fragile
// fade-overwrites / NOP padding in the Cave-of-the-Gi work.
//
// Correctness gate: parse(field) followed by emit() with NO edits must return
// a byte-identical field. selfTestRoundTrip() asserts this; run it over every
// field in a flevel before trusting an edit (see runRoundTripSelfTest()).
//
// The editor operates on a DECOMPRESSED field (one flevel sub-file). Callers
// decompress with the same LZS helpers used elsewhere, edit, emit, recompress.
// ============================================================================
#ifndef FIELDSCRIPTEDITOR_H
#define FIELDSCRIPTEDITOR_H

#include <QByteArray>
#include <QString>
#include <QVector>

class FieldScriptEditor
{
public:
    FieldScriptEditor() = default;

    // Parse a decompressed field file. Returns false if the script section
    // can't be cleanly modelled (caller should fall back to byte patching).
    // `err` receives a human-readable reason on failure.
    bool parse(const QByteArray &field, QString &err);

    bool isValid() const { return m_valid; }
    int  entityCount() const { return m_nbEntities; }

    // Instruction index at which (entity, script 0..31) begins, or -1.
    // Multiple empty scripts may share one index (they point at the same RET).
    int scriptStart(int entity, int script) const;

    // First instruction (>= fromInstr) whose opcode bytes begin with `prefix`.
    // Matches against the opcode's own bytes (id + operands), so a 1-byte
    // prefix matches by opcode id. Returns -1 if not found.
    int findOpcode(const QByteArray &prefix, int fromInstr = 0) const;

    // First instruction (>= fromInstr) at whose opcode boundary the byte pattern
    // `bytes` begins (the pattern may span several consecutive opcodes). Use to
    // reuse the existing multi-opcode byte anchors. Returns -1 if not found.
    int findBytes(const QByteArray &bytes, int fromInstr = 0) const;

    // Total number of decoded instructions in the bytecode stream.
    int instrCount() const { return int(m_instrs.size()); }

    // The raw bytes of instruction `idx` (id + operands).
    QByteArray instrBytes(int idx) const;

    // --- editing ------------------------------------------------------------
    // Each takes/returns instruction indices in the CURRENT list; indices of
    // instructions after an insert/remove shift accordingly. Jump targets and
    // script starts are tracked symbolically, so they survive edits.
    //
    // `opcodeBytes` must be one or more complete, valid opcodes. They are
    // inserted as non-jump instructions (use for SOLID/IDLCK/BITON/etc.).

    // Insert before instruction `idx` (idx == instrCount() appends at end).
    bool insertBefore(int idx, const QByteArray &opcodeBytes, QString &err);

    // Replace instruction `idx` with `opcodeBytes` (may differ in length).
    bool replaceAt(int idx, const QByteArray &opcodeBytes, QString &err);

    // Overwrite operand bytes of instruction `idx` in place (length & opcode id
    // unchanged), preserving its symbolic jump target. Use for IF-comparison
    // tweaks (value/oper) on a jump opcode without disturbing its label. Must
    // not touch byte 0 (the id) and must stay within the opcode; on a jump
    // opcode it must not overlap the trailing jump operand (that is recomputed
    // by assemble()).
    bool patchOperands(int idx, int byteOffset, const QByteArray &bytes, QString &err);

    // Remove instruction `idx`.
    bool removeAt(int idx, QString &err);

    // --- assemble -----------------------------------------------------------
    // Reconstruct the whole field with recomputed offsets/jumps. Returns an
    // empty array and sets err on failure (e.g. an unresolvable jump).
    // (Named "assemble" rather than "emit" — the latter is a Qt macro.)
    QByteArray assemble(QString &err) const;

    // parse(field) then assemble() == field. Sets err with the first divergence.
    static bool selfTestRoundTrip(const QByteArray &field, QString &err);

private:
    struct Instr {
        QByteArray bytes;   // opcode id + operands (current)
        int        target;  // jump target instruction index, or -1 if not a jump
        bool       isJump;
        bool       backJump;
        bool       longJump;
        int        shift;   // jumpShift for this opcode
    };

    // opcode metadata helpers (self-contained; mirror Makou Reactor)
    static int  opcodeLength(const QByteArray &d, int pos);
    static bool opIsJump(quint8 id);
    static bool opIsBackJump(quint8 id);
    static bool opIsLongJump(quint8 id);
    static int  opJumpShift(quint8 id);
    static quint8 opLongVariant(quint8 shortId);   // 0 if none

    // read/compute the signed logical jump (target = pos + jump())
    static int  readJump(const Instr &in);
    // set the signed logical jump, possibly promoting short->long; returns
    // true if the instruction byte-length changed.
    bool        writeJump(Instr &in, int jump, QString &err, bool &sizeChanged) const;

    bool m_valid = false;

    // whole original field + parsed layout
    QByteArray m_field;
    int  m_sec0LenPos = 0;      // position of section-0 u32 length prefix
    int  m_sec0DataStart = 0;   // == m_sec0LenPos + 4
    int  m_sectionLen = 0;      // original section-0 data length
    int  m_nbEntities = 0;
    int  m_nAkao = 0;
    int  m_wStringOffset = 0;   // string table offset (rel sec0DataStart)
    int  m_scriptTableOff = 0;  // abs pos of the 32*nb u16 offset table
    int  m_bcStartRel = 0;      // bytecode start (rel sec0DataStart)
    int  m_origBcLen = 0;       // original bytecode length

    QVector<quint32> m_akaoOffsets;       // rel sec0DataStart
    QVector<int>     m_scriptStartIdx;    // [entity*32 + script] -> instr index (or end)
    QVector<Instr>   m_instrs;

    // raw bytes after the bytecode within section 0 (string table + akao blocks)
    QByteArray m_tail;
    // raw section-0 prologue: 32-byte header + names + akao table + script table
    // (regenerated on emit from the parsed pieces, so we keep names verbatim)
    QByteArray m_names;   // 8*nb bytes
    QByteArray m_header;  // 32 bytes
};

#endif // FIELDSCRIPTEDITOR_H
