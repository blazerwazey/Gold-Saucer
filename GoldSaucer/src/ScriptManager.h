#pragma once

#include <QByteArray>
#include <QList>

/**
 * Simplified MESSAGE opcode for Text ID management
 * Focused only on the functionality needed for shiftTextIds
 */
struct MessageOpcode
{
    quint8 opcode;      // Should be 0x40 for MESSAGE
    quint8 textId;      // Text ID parameter
    
    MessageOpcode() : opcode(0x40), textId(0) {}
    MessageOpcode(quint8 textId) : opcode(0x40), textId(textId) {}
    
    bool isValid() const { return opcode == 0x40; }
};

/**
 * Simplified Script class for Text ID management
 * Ported from Makou Reactor's Script class, but focused only on MESSAGE opcodes
 */
class ScriptManager
{
public:
    ScriptManager();
    explicit ScriptManager(const QByteArray &scriptData);
    
    // Text ID management (ported from Makou)
    void shiftTextIds(quint8 fromTextId, qint16 steps);
    
    // MESSAGE opcode operations
    QList<MessageOpcode> getMessageOpcodes() const;
    void updateMessageOpcode(int position, quint8 newTextId);
    
    // Serialization
    QByteArray toByteArray() const;
    
    // Validation
    bool isValid() const;
    bool isEmpty() const;

private:
    QByteArray _scriptData;
    QList<MessageOpcode> _messageOpcodes;
    
    void parseMessageOpcodes();
    void rebuildScriptData();
};
