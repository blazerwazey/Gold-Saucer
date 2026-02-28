#include "ScriptManager.h"

ScriptManager::ScriptManager()
{
}

ScriptManager::ScriptManager(const QByteArray &scriptData) : _scriptData(scriptData)
{
    parseMessageOpcodes();
}

void ScriptManager::shiftTextIds(quint8 fromTextId, qint16 steps)
{
    for (int i = 0; i < _messageOpcodes.size(); ++i) {
        MessageOpcode &opcode = _messageOpcodes[i];
        
        // Update Text IDs >= fromTextId
        if (opcode.textId >= fromTextId) {
            quint16 newTextId = static_cast<quint16>(opcode.textId + steps);
            
            // Check for overflow
            if ((steps > 0 && newTextId < opcode.textId) || 
                (steps < 0 && newTextId > opcode.textId)) {
                continue; // Skip if overflow would occur
            }
            
            // Update the Text ID
            opcode.textId = static_cast<quint8>(newTextId);
            
            // Update the raw script data
            updateMessageOpcode(i, opcode.textId);
        }
    }
}

QList<MessageOpcode> ScriptManager::getMessageOpcodes() const
{
    return _messageOpcodes;
}

void ScriptManager::updateMessageOpcode(int position, quint8 newTextId)
{
    if (position < 0 || position >= _messageOpcodes.size()) {
        return;
    }
    
    // Find the actual position of this MESSAGE opcode in the script data
    int opcodeCount = 0;
    for (int i = 0; i < _scriptData.size(); ) {
        quint8 opcode = static_cast<quint8>(_scriptData[i]);
        
        if (opcode == 0x40) { // MESSAGE opcode
            if (opcodeCount == position) {
                // Update the Text ID parameter
                if (i + 1 < _scriptData.size()) {
                    _scriptData[i + 1] = static_cast<char>(newTextId);
                    _messageOpcodes[position].textId = newTextId;
                }
                break;
            }
            opcodeCount++;
            i += 2; // MESSAGE opcode is 2 bytes
        } else if (opcode == 0xE0) { // Entity script boundary
            i += 4; // Skip entity header
        } else {
            i += 1; // Skip other opcodes
        }
    }
}

QByteArray ScriptManager::toByteArray() const
{
    return _scriptData;
}

bool ScriptManager::isValid() const
{
    return !_scriptData.isEmpty();
}

bool ScriptManager::isEmpty() const
{
    return _scriptData.isEmpty();
}

void ScriptManager::parseMessageOpcodes()
{
    _messageOpcodes.clear();
    
    for (int i = 0; i < _scriptData.size(); ) {
        quint8 opcode = static_cast<quint8>(_scriptData[i]);
        
        if (opcode == 0x40) { // MESSAGE opcode
            if (i + 1 < _scriptData.size()) {
                quint8 textId = static_cast<quint8>(_scriptData[i + 1]);
                _messageOpcodes.append(MessageOpcode(textId));
            }
            i += 2; // MESSAGE opcode is 2 bytes
        } else if (opcode == 0xE0) { // Entity script boundary
            i += 4; // Skip entity header
        } else {
            i += 1; // Skip other opcodes
        }
    }
}

void ScriptManager::rebuildScriptData()
{
    // This would be needed if we were doing more complex script manipulation
    // For our current use case, we update the script data directly in updateMessageOpcode
}
