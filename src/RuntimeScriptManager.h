#pragma once

#include <Arduino.h>

class Print;

class RuntimeScriptManager {
public:
    bool begin();
    bool storageReady() const { return _storageReady; }

    void beginUpload();
    bool appendUploadLine(const char* line, String* error = nullptr);
    void cancelUpload();
    bool hasStagedScript() const { return _stagedSource.length() > 0; }
    size_t stagedBytes() const { return _stagedSource.length(); }

    bool commitUpload(String* error = nullptr);
    bool saveStagedScript(const char* fileName, String* error = nullptr) const;
    bool loadScriptFile(const char* fileName, String* error = nullptr);
    bool deleteScriptFile(const char* fileName, String* error = nullptr);
    bool installBankSlotFromStaged(uint8_t slot, String* error = nullptr);
    bool installBankSlotFromFile(uint8_t slot, const char* fileName, String* error = nullptr);
    bool installBuiltinBankSlot(uint8_t builtinId, uint8_t slot = 0, String* error = nullptr);
    bool reseedBuiltinBankSlot(uint8_t slot, String* error = nullptr);
    void listBankSlots(Print& out) const;
    void listBuiltinBankSlots(Print& out) const;
    String bankSlotName(uint8_t slot) const;
    bool setStartupScriptFile(const char* fileName, String* error = nullptr);
    bool clearStartupScriptFile(String* error = nullptr);
    bool loadStartupScript(String* error = nullptr);
    String startupScriptPath() const;

    void listScriptFiles(Print& out) const;
    void listRuntimeScripts(Print& out) const;

    bool unloadRuntimeScript(uint8_t id, String* error = nullptr);

    uint8_t lastInstalledId() const { return _lastInstalledId; }
    const String& lastInstalledName() const { return _lastInstalledName; }

private:
    bool _storageReady = false;
    bool _uploadActive = false;
    String _stagedSource;
    uint8_t _lastInstalledId = 0;
    String _lastInstalledName;

    bool _parseAndInstall(const String& source, String* error, int forcedScriptId = -1);
    bool _readScriptFile(const String& path, String* source, String* error) const;
    bool _writeScriptFile(const String& path, const String& source, String* error) const;
    bool _loadBankSlots(String* error = nullptr);
    bool _validBankSlot(uint8_t slot) const;
    String _bankSlotPath(uint8_t slot) const;
    String _normalizePath(const char* fileName) const;
};
