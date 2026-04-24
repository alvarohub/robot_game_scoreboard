#include "RuntimeScriptManager.h"

#include <FS.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <new>

#include "Animations.h"

namespace {

static constexpr size_t MAX_STAGED_SOURCE_BYTES = 4096;
static constexpr uint8_t MAX_SCRIPT_STEPS = 64;
static constexpr uint8_t MAX_SCRIPT_LABELS = 64;
static constexpr uint8_t MAX_SCRIPT_GOTOS = 64;
static constexpr size_t MAX_LABEL_LEN = 31;
static constexpr size_t MAX_NAME_LEN = 63;
static constexpr size_t MAX_TOKEN_LEN = 95;
static constexpr size_t MAX_TOKENS_PER_LINE = 8;
static constexpr const char* STARTUP_PREFS_NAMESPACE = "scripts";
static constexpr const char* STARTUP_PREFS_KEY = "startup";
static constexpr const char* EMPTY_BANK_SCRIPT_NAME = "no script";

struct BuiltinBankTemplate {
    uint8_t id;
    uint8_t defaultSlot;
    const char* name;
    const char* seedKey;
    const char* body;
};

static constexpr BuiltinBankTemplate BUILTIN_BANK_TEMPLATES[] = {
    {
        1,
        4,
        "water default",
        "builtin.water.v1",
        R"SCRIPT(step setup
mode text
text off
particles on
physics running
replace_particle_config true
particle count 64
particle renderMs 6
particle substepMs 26
particle gravityScale 45.194805
particle gravityEnabled true
particle collisionEnabled true
particle elasticity 0.797297
particle wallElasticity 0.824324
particle damping 0.9996
particle radius 0.408108
particle renderStyle glow
particle glowSigma 0.6
particle glowWavelength 0.0
particle temperature 0.0
particle attractStrength 0.054054
particle attractRange 1.763514
particle speedColor true
particle springStrength 0.0
particle springRange 5.0
particle springEnabled false
particle coulombStrength 0.0
particle coulombRange 10.0
particle coulombEnabled false
particle scaffoldStrength 0.0
particle scaffoldRange 10.0
particle scaffoldEnabled false
end
)SCRIPT"
    },
};

static constexpr size_t BUILTIN_BANK_TEMPLATE_COUNT =
    sizeof(BUILTIN_BANK_TEMPLATES) / sizeof(BUILTIN_BANK_TEMPLATES[0]);

// Use fixed scratch storage during parsing so commit does not depend on a
// large transient heap allocation on the device.
AnimationStep g_parseScratchSteps[MAX_SCRIPT_STEPS];

String buildNoScriptStub(uint8_t slot) {
    String source;
    source.reserve(48);
    source += "id ";
    source += String(slot);
    source += "\nname ";
    source += EMPTY_BANK_SCRIPT_NAME;
    source += "\n";
    source += "step idle\n";
    source += "end\n";
    return source;
}

const BuiltinBankTemplate* findBuiltinBankTemplateById(uint8_t builtinId) {
    for (size_t i = 0; i < BUILTIN_BANK_TEMPLATE_COUNT; ++i) {
        if (BUILTIN_BANK_TEMPLATES[i].id == builtinId) {
            return &BUILTIN_BANK_TEMPLATES[i];
        }
    }
    return nullptr;
}

const BuiltinBankTemplate* findBuiltinBankTemplateByDefaultSlot(uint8_t slot) {
    for (size_t i = 0; i < BUILTIN_BANK_TEMPLATE_COUNT; ++i) {
        if (BUILTIN_BANK_TEMPLATES[i].defaultSlot == slot) {
            return &BUILTIN_BANK_TEMPLATES[i];
        }
    }
    return nullptr;
}

String buildBuiltinBankSource(const BuiltinBankTemplate& builtin, uint8_t slot) {
    String source;
    source.reserve(768);
    source += "id ";
    source += String(slot);
    source += "\nname ";
    source += builtin.name;
    source += "\n";
    source += builtin.body;
    return source;
}

bool shouldSeedBuiltinBankSlot(const BuiltinBankTemplate& builtin) {
    Preferences prefs;
    if (!prefs.begin(STARTUP_PREFS_NAMESPACE, true)) {
        return true;
    }
    bool seeded = prefs.getBool(builtin.seedKey, false);
    prefs.end();
    return !seeded;
}

void markBuiltinBankSlotSeeded(const BuiltinBankTemplate& builtin) {
    Preferences prefs;
    if (!prefs.begin(STARTUP_PREFS_NAMESPACE, false)) {
        return;
    }
    prefs.putBool(builtin.seedKey, true);
    prefs.end();
}

bool isNoScriptStubSource(const String& source) {
    String normalized = source;
    normalized.replace("\r", "");
    normalized.trim();
    return normalized.startsWith(String("name ") + EMPTY_BANK_SCRIPT_NAME + "\n") ||
           normalized.indexOf(String("\nname ") + EMPTY_BANK_SCRIPT_NAME + "\n") >= 0;
}

struct ScriptLabel {
    char name[MAX_LABEL_LEN + 1];
    uint8_t stepIndex = 0;
};

struct PendingGoto {
    char label[MAX_LABEL_LEN + 1];
    uint8_t stepIndex = 0;
};

bool setError(String* error, const String& message) {
    if (error) *error = message;
    return false;
}

char* dupCString(const String& value) {
    char* copy = new (std::nothrow) char[value.length() + 1];
    if (!copy) {
        return nullptr;
    }
    memcpy(copy, value.c_str(), value.length() + 1);
    return copy;
}

String joinTokens(char tokens[][MAX_TOKEN_LEN + 1], uint8_t start, uint8_t count) {
    String out;
    for (uint8_t i = start; i < count; ++i) {
        if (i > start) out += ' ';
        out += tokens[i];
    }
    return out;
}

void stripComments(char* line) {
    bool inQuotes = false;
    for (char* p = line; *p; ++p) {
        if (*p == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (*p == '#' && !inQuotes) {
            *p = '\0';
            return;
        }
    }
}

void trimLine(char* line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }

    size_t start = 0;
    while (line[start] == ' ' || line[start] == '\t') {
        start++;
    }
    if (start > 0) {
        memmove(line, line + start, strlen(line + start) + 1);
    }
}

bool tokenizeLine(const char* line, char tokens[][MAX_TOKEN_LEN + 1], uint8_t* outCount, String* error) {
    *outCount = 0;
    const char* p = line;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (*outCount >= MAX_TOKENS_PER_LINE) {
            return setError(error, "too many tokens on one line");
        }

        size_t len = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (len >= MAX_TOKEN_LEN) {
                    return setError(error, "token too long");
                }
                tokens[*outCount][len++] = *p++;
            }
            if (*p != '"') {
                return setError(error, "unterminated quoted string");
            }
            p++;
        } else {
            while (*p && *p != ' ' && *p != '\t') {
                if (len >= MAX_TOKEN_LEN) {
                    return setError(error, "token too long");
                }
                tokens[*outCount][len++] = *p++;
            }
        }

        tokens[*outCount][len] = '\0';
        (*outCount)++;
    }

    return true;
}

bool parseBoolToken(const char* token, bool* out) {
    if (!token || !out) return false;
    String value(token);
    value.toLowerCase();
    if (value == "1" || value == "true" || value == "on" || value == "yes" || value == "enabled") {
        *out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no" || value == "disabled") {
        *out = false;
        return true;
    }
    return false;
}

bool parseTextEnabledToken(const char* token, bool* out) {
    if (!token || !out) return false;
    if (parseBoolToken(token, out)) {
        return true;
    }

    // Legacy shorthand such as `text hello` is treated as `text on`.
    // Runtime scripts still do not carry a text payload per step.
    *out = true;
    return true;
}

bool parseLongToken(const char* token, long* out) {
    if (!token || !out) return false;
    char* end = nullptr;
    long value = strtol(token, &end, 10);
    if (end == token || *end != '\0') return false;
    *out = value;
    return true;
}

bool parseFloatToken(const char* token, float* out) {
    if (!token || !out) return false;
    char* end = nullptr;
    float value = strtof(token, &end);
    if (end == token || *end != '\0') return false;
    *out = value;
    return true;
}

bool parseModeToken(const char* token, DisplayMode* out) {
    if (!token || !out) return false;
    String value(token);
    value.toLowerCase();
    if (value == "text") {
        *out = DISPLAY_MODE_TEXT;
        return true;
    }
    if (value == "scroll_up" || value == "scrollup") {
        *out = DISPLAY_MODE_SCROLL_UP;
        return true;
    }
    if (value == "scroll_down" || value == "scrolldown") {
        *out = DISPLAY_MODE_SCROLL_DOWN;
        return true;
    }
    return false;
}

bool parseDisplayMaskToken(const char* token, uint32_t* outMask) {
    if (!token || !outMask) return false;

    String value(token);
    value.trim();
    value.toLowerCase();
    if (value == "all" || value == "*" || value == "0") {
        *outMask = 0;  // 0 => all displays
        return true;
    }

    long displayNumber = 0;
    if (!parseLongToken(token, &displayNumber)) {
        return false;
    }
    if (displayNumber < 1 || displayNumber > NUM_DISPLAYS) {
        return false;
    }

    *outMask = (1ul << (uint32_t)(displayNumber - 1));
    return true;
}

bool parseRenderStyleToken(const char* token, ParticleModeConfig::RenderStyle* out) {
    if (!token || !out) return false;
    String value(token);
    value.toLowerCase();
    if (value == "point") {
        *out = ParticleModeConfig::RENDER_POINT;
        return true;
    }
    if (value == "square") {
        *out = ParticleModeConfig::RENDER_SQUARE;
        return true;
    }
    if (value == "circle") {
        *out = ParticleModeConfig::RENDER_CIRCLE;
        return true;
    }
    if (value == "text") {
        *out = ParticleModeConfig::RENDER_TEXT;
        return true;
    }
    if (value == "glow") {
        *out = ParticleModeConfig::RENDER_GLOW;
        return true;
    }
    return false;
}

bool setParticleField(AnimationStep& step, const char* field, const char* valueToken, String* error) {
    if (!field || !valueToken) {
        return setError(error, "particle command requires field and value");
    }

    step.setParticleConfig = true;
    String name(field);
    name.toLowerCase();

    long intValue = 0;
    float floatValue = 0.0f;
    bool boolValue = false;
    ParticleModeConfig::RenderStyle renderStyle = ParticleModeConfig::RENDER_GLOW;

    if (name == "count") {
        if (!parseLongToken(valueToken, &intValue)) return setError(error, "invalid particle count");
        step.particleConfig.count = (uint8_t)intValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_COUNT;
        return true;
    }
    if (name == "renderms") {
        if (!parseLongToken(valueToken, &intValue)) return setError(error, "invalid renderMs");
        step.particleConfig.renderMs = (uint8_t)intValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_RENDER_MS;
        return true;
    }
    if (name == "substepms") {
        if (!parseLongToken(valueToken, &intValue)) return setError(error, "invalid substepMs");
        step.particleConfig.substepMs = (uint8_t)intValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_SUBSTEP_MS;
        return true;
    }
    if (name == "radius") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid radius");
        step.particleConfig.radius = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_RADIUS;
        return true;
    }
    if (name == "gravityscale") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid gravityScale");
        step.particleConfig.gravityScale = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_GRAVITY_SCALE;
        return true;
    }
    if (name == "elasticity") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid elasticity");
        step.particleConfig.elasticity = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_ELASTICITY;
        return true;
    }
    if (name == "wallelasticity") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid wallElasticity");
        step.particleConfig.wallElasticity = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_WALL_ELASTICITY;
        return true;
    }
    if (name == "damping") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid damping");
        step.particleConfig.damping = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_DAMPING;
        return true;
    }
    if (name == "temperature") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid temperature");
        step.particleConfig.temperature = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_TEMPERATURE;
        return true;
    }
    if (name == "attractstrength") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid attractStrength");
        step.particleConfig.attractStrength = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_ATTRACT_STRENGTH;
        return true;
    }
    if (name == "attractrange") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid attractRange");
        step.particleConfig.attractRange = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_ATTRACT_RANGE;
        return true;
    }
    if (name == "gravityenabled") {
        if (!parseBoolToken(valueToken, &boolValue)) return setError(error, "invalid gravityEnabled");
        step.particleConfig.gravityEnabled = boolValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_GRAVITY_ENABLED;
        return true;
    }
    if (name == "collisionenabled") {
        if (!parseBoolToken(valueToken, &boolValue)) return setError(error, "invalid collisionEnabled");
        step.particleConfig.collisionEnabled = boolValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_COLLISION_ENABLED;
        return true;
    }
    if (name == "springstrength") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid springStrength");
        step.particleConfig.springStrength = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_SPRING_STRENGTH;
        return true;
    }
    if (name == "springrange") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid springRange");
        step.particleConfig.springRange = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_SPRING_RANGE;
        return true;
    }
    if (name == "springenabled") {
        if (!parseBoolToken(valueToken, &boolValue)) return setError(error, "invalid springEnabled");
        step.particleConfig.springEnabled = boolValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_SPRING_ENABLED;
        return true;
    }
    if (name == "coulombstrength") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid coulombStrength");
        step.particleConfig.coulombStrength = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_COULOMB_STRENGTH;
        return true;
    }
    if (name == "coulombrange") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid coulombRange");
        step.particleConfig.coulombRange = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_COULOMB_RANGE;
        return true;
    }
    if (name == "coulombenabled") {
        if (!parseBoolToken(valueToken, &boolValue)) return setError(error, "invalid coulombEnabled");
        step.particleConfig.coulombEnabled = boolValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_COULOMB_ENABLED;
        return true;
    }
    if (name == "scaffoldstrength") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid scaffoldStrength");
        step.particleConfig.scaffoldStrength = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_SCAFFOLD_STRENGTH;
        return true;
    }
    if (name == "scaffoldrange") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid scaffoldRange");
        step.particleConfig.scaffoldRange = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_SCAFFOLD_RANGE;
        return true;
    }
    if (name == "scaffoldenabled") {
        if (!parseBoolToken(valueToken, &boolValue)) return setError(error, "invalid scaffoldEnabled");
        step.particleConfig.scaffoldEnabled = boolValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_SCAFFOLD_ENABLED;
        return true;
    }
    if (name == "renderstyle") {
        if (!parseRenderStyleToken(valueToken, &renderStyle)) return setError(error, "invalid renderStyle");
        step.particleConfig.renderStyle = renderStyle;
        step.particleFieldMask |= AnimationStep::PARTICLE_RENDER_STYLE;
        return true;
    }
    if (name == "glowsigma") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid glowSigma");
        step.particleConfig.glowSigma = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_GLOW_SIGMA;
        return true;
    }
    if (name == "glowwavelength") {
        if (!parseFloatToken(valueToken, &floatValue)) return setError(error, "invalid glowWavelength");
        step.particleConfig.glowWavelength = floatValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_GLOW_WAVELENGTH;
        return true;
    }
    if (name == "speedcolor") {
        if (!parseBoolToken(valueToken, &boolValue)) return setError(error, "invalid speedColor");
        step.particleConfig.speedColor = boolValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_SPEED_COLOR;
        return true;
    }
    if (name == "physicspaused") {
        if (!parseBoolToken(valueToken, &boolValue)) return setError(error, "invalid physicsPaused");
        step.particleConfig.physicsPaused = boolValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_PHYSICS_PAUSED;
        return true;
    }
    if (name == "textindex") {
        if (!parseLongToken(valueToken, &intValue)) return setError(error, "invalid textIndex");
        step.particleConfig.textIndex = (uint8_t)intValue;
        step.particleFieldMask |= AnimationStep::PARTICLE_TEXT_INDEX;
        return true;
    }

    return setError(error, String("unknown particle field '") + field + "'");
}

}  // namespace

bool RuntimeScriptManager::begin() {
    if (_storageReady) {
        return true;
    }
    _storageReady = SPIFFS.begin(false);
    if (!_storageReady) {
        Serial.println("Runtime scripts: SPIFFS unavailable");
        return false;
    }
    Serial.println("Runtime scripts: SPIFFS ready");
    String err;
    if (!_loadBankSlots(&err) && err.length() > 0) {
        Serial.printf("Runtime scripts: bank load warning: %s\n", err.c_str());
    }
    return true;
}

void RuntimeScriptManager::beginUpload() {
    _uploadActive = true;
    _stagedSource = "";
}

bool RuntimeScriptManager::appendUploadLine(const char* line, String* error) {
    if (!_uploadActive) {
        return setError(error, "no active script upload; call /script/begin first");
    }

    String append = line ? String(line) : String("");
    if (_stagedSource.length() + append.length() + 1 > MAX_STAGED_SOURCE_BYTES) {
        return setError(error, "staged script exceeds size limit");
    }

    _stagedSource += append;
    _stagedSource += '\n';
    return true;
}

void RuntimeScriptManager::cancelUpload() {
    _uploadActive = false;
    _stagedSource = "";
}

bool RuntimeScriptManager::commitUpload(String* error) {
    if (_stagedSource.length() == 0) {
        return setError(error, "no staged script to commit");
    }
    _uploadActive = false;
    return _parseAndInstall(_stagedSource, error);
}

bool RuntimeScriptManager::saveStagedScript(const char* fileName, String* error) const {
    if (!_storageReady) {
        return setError(error, "script storage is not available");
    }
    if (_stagedSource.length() == 0) {
        return setError(error, "no staged script to save");
    }

    String path = _normalizePath(fileName);
    if (path.length() == 0) {
        return setError(error, "invalid script file name");
    }

    return _writeScriptFile(path, _stagedSource, error);
}

bool RuntimeScriptManager::loadScriptFile(const char* fileName, String* error) {
    if (!_storageReady) {
        return setError(error, "script storage is not available");
    }

    String path = _normalizePath(fileName);
    if (path.length() == 0) {
        return setError(error, "invalid script file name");
    }

    String source;
    if (!_readScriptFile(path, &source, error)) {
        return false;
    }

    if (!_parseAndInstall(source, error)) {
        return false;
    }

    _stagedSource = source;
    _uploadActive = false;
    return true;
}

bool RuntimeScriptManager::installBankSlotFromStaged(uint8_t slot, String* error) {
    if (!_storageReady) {
        return setError(error, "script storage is not available");
    }
    if (!_validBankSlot(slot)) {
        return setError(error, "invalid animation bank slot");
    }
    if (_stagedSource.length() == 0) {
        return setError(error, "no staged script to install");
    }

    String slotPath = _bankSlotPath(slot);
    if (!_writeScriptFile(slotPath, _stagedSource, error)) {
        return false;
    }
    _uploadActive = false;
    return _parseAndInstall(_stagedSource, error, slot);
}

bool RuntimeScriptManager::installBankSlotFromFile(uint8_t slot, const char* fileName, String* error) {
    if (!_storageReady) {
        return setError(error, "script storage is not available");
    }
    if (!_validBankSlot(slot)) {
        return setError(error, "invalid animation bank slot");
    }

    String path = _normalizePath(fileName);
    if (path.length() == 0) {
        return setError(error, "invalid script file name");
    }

    String source;
    if (!_readScriptFile(path, &source, error)) {
        return false;
    }

    String slotPath = _bankSlotPath(slot);
    if (!_writeScriptFile(slotPath, source, error)) {
        return false;
    }

    _stagedSource = source;
    _uploadActive = false;
    return _parseAndInstall(source, error, slot);
}

bool RuntimeScriptManager::installBuiltinBankSlot(uint8_t builtinId, uint8_t slot, String* error) {
    if (!_storageReady) {
        return setError(error, "script storage is not available");
    }

    const BuiltinBankTemplate* builtin = findBuiltinBankTemplateById(builtinId);
    if (!builtin) {
        return setError(error, String("no built-in bank with id ") + builtinId);
    }

    uint8_t targetSlot = slot == 0 ? builtin->defaultSlot : slot;
    if (!_validBankSlot(targetSlot)) {
        return setError(error, "invalid animation bank slot");
    }

    String source = buildBuiltinBankSource(*builtin, targetSlot);
    String slotPath = _bankSlotPath(targetSlot);
    if (!_writeScriptFile(slotPath, source, error)) {
        return false;
    }

    _stagedSource = source;
    _uploadActive = false;
    return _parseAndInstall(source, error, targetSlot);
}

bool RuntimeScriptManager::reseedBuiltinBankSlot(uint8_t slot, String* error) {
    const BuiltinBankTemplate* builtin = findBuiltinBankTemplateByDefaultSlot(slot);
    if (!builtin) {
        return setError(error, String("no built-in preset is assigned to bank slot ") + slot);
    }
    if (!installBuiltinBankSlot(builtin->id, slot, error)) {
        return false;
    }
    markBuiltinBankSlotSeeded(*builtin);
    return true;
}

bool RuntimeScriptManager::deleteScriptFile(const char* fileName, String* error) {
    if (!_storageReady) {
        return setError(error, "script storage is not available");
    }

    String path = _normalizePath(fileName);
    if (path.length() == 0) {
        return setError(error, "invalid script file name");
    }
    if (!SPIFFS.exists(path)) {
        return setError(error, String("no stored script at ") + path);
    }
    if (!SPIFFS.remove(path)) {
        return setError(error, String("failed to delete ") + path);
    }
    if (startupScriptPath() == path) {
        clearStartupScriptFile();
    }
    return true;
}

bool RuntimeScriptManager::setStartupScriptFile(const char* fileName, String* error) {
    String path = _normalizePath(fileName);
    if (path.length() == 0) {
        return setError(error, "invalid startup script file name");
    }

    Preferences prefs;
    if (!prefs.begin(STARTUP_PREFS_NAMESPACE, false)) {
        return setError(error, "failed to open startup script preferences");
    }
    prefs.putString(STARTUP_PREFS_KEY, path);
    prefs.end();
    return true;
}

bool RuntimeScriptManager::clearStartupScriptFile(String* error) {
    Preferences prefs;
    if (!prefs.begin(STARTUP_PREFS_NAMESPACE, false)) {
        return setError(error, "failed to open startup script preferences");
    }
    prefs.remove(STARTUP_PREFS_KEY);
    prefs.end();
    return true;
}

String RuntimeScriptManager::startupScriptPath() const {
    Preferences prefs;
    if (!prefs.begin(STARTUP_PREFS_NAMESPACE, true)) {
        return String();
    }
    String path = prefs.getString(STARTUP_PREFS_KEY, "");
    prefs.end();
    return path;
}

bool RuntimeScriptManager::loadStartupScript(String* error) {
    String path = startupScriptPath();
    if (path.length() == 0) {
        return true;
    }
    return loadScriptFile(path.c_str(), error);
}

void RuntimeScriptManager::listScriptFiles(Print& out) const {
    if (!_storageReady) {
        out.println("SCRIPT_STORAGE OFF");
        return;
    }

    File root = SPIFFS.open("/");
    if (!root) {
        out.println("SCRIPT_FILES OPEN_FAILED");
        return;
    }

    bool found = false;
    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (name.endsWith(".game")) {
            out.printf("SCRIPT_FILE %s %u\n", name.c_str(), (unsigned)file.size());
            found = true;
        }
        file = root.openNextFile();
    }
    if (!found) {
        out.println("SCRIPT_FILE NONE");
    }
}

void RuntimeScriptManager::listBankSlots(Print& out) const {
    String summary = "BANK_SLOTS ";
    for (uint8_t slot = 1; slot <= ANIMATION_BANK_SLOT_COUNT; ++slot) {
        if (slot > 1) {
            summary += ';';
        }
        summary += String(slot);
        summary += ':';

        const AnimationScript* script = findRuntimeAnimationScript(slot);
        if (!script || !script->name || script->name[0] == '\0') {
            summary += "no script";
        } else {
            summary += script->name;
        }
    }
    out.println(summary);
}

void RuntimeScriptManager::listBuiltinBankSlots(Print& out) const {
    if (BUILTIN_BANK_TEMPLATE_COUNT == 0) {
        out.println("BUILTIN_BANK NONE");
        return;
    }

    for (size_t i = 0; i < BUILTIN_BANK_TEMPLATE_COUNT; ++i) {
        const BuiltinBankTemplate& builtin = BUILTIN_BANK_TEMPLATES[i];
        out.printf("BUILTIN_BANK %u %u %s\n", builtin.id, builtin.defaultSlot, builtin.name);
    }
}

String RuntimeScriptManager::bankSlotName(uint8_t slot) const {
    if (!_validBankSlot(slot)) {
        return String();
    }
    const AnimationScript* script = findRuntimeAnimationScript(slot);
    if (!script || !script->name || script->name[0] == '\0') {
        return String("no script");
    }
    return String(script->name);
}

void RuntimeScriptManager::listRuntimeScripts(Print& out) const {
    uint8_t count = runtimeAnimationScriptCount();
    if (count == 0) {
        out.println("RUNTIME_SCRIPT NONE");
        return;
    }

    for (uint8_t i = 0; i < count; ++i) {
        const AnimationScript* script = runtimeAnimationScriptAt(i);
        if (!script) continue;
        out.printf("RUNTIME_SCRIPT %u %s %u\n", script->id, script->name, script->stepCount);
    }
}

bool RuntimeScriptManager::unloadRuntimeScript(uint8_t id, String* error) {
    if (!clearRuntimeAnimationScript(id)) {
        return setError(error, String("no runtime script with id ") + id);
    }
    return true;
}

bool RuntimeScriptManager::_readScriptFile(const String& path, String* source, String* error) const {
    if (!source) {
        return setError(error, "internal error: missing source buffer");
    }

    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        return setError(error, String("failed to open ") + path);
    }

    source->reserve((size_t)file.size());
    while (file.available()) {
        *source += (char)file.read();
        if (source->length() > MAX_STAGED_SOURCE_BYTES) {
            file.close();
            return setError(error, "stored script exceeds size limit");
        }
    }
    file.close();
    return true;
}

bool RuntimeScriptManager::_writeScriptFile(const String& path, const String& source, String* error) const {
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) {
        return setError(error, String("failed to open ") + path + " for write");
    }
    if (file.print(source) != source.length()) {
        file.close();
        return setError(error, String("failed to write ") + path);
    }
    file.close();
    return true;
}

bool RuntimeScriptManager::_loadBankSlots(String* error) {
    bool loadedAny = false;

    for (uint8_t slot = 1; slot <= ANIMATION_BANK_SLOT_COUNT; ++slot) {
        const BuiltinBankTemplate* builtin = findBuiltinBankTemplateByDefaultSlot(slot);
        bool shouldSeedBuiltin = builtin && shouldSeedBuiltinBankSlot(*builtin);
        String path = _bankSlotPath(slot);
        String defaultSource = shouldSeedBuiltin ? buildBuiltinBankSource(*builtin, slot) : buildNoScriptStub(slot);
        if (!SPIFFS.exists(path)) {
            String slotError;
            if (!_writeScriptFile(path, defaultSource, &slotError) || !_parseAndInstall(defaultSource, &slotError, slot)) {
                clearRuntimeAnimationScript(slot);
                if (error && error->length() == 0) {
                    *error = String("slot ") + slot + ": " + slotError;
                }
                continue;
            }
            if (shouldSeedBuiltin) {
                markBuiltinBankSlotSeeded(*builtin);
            }
            loadedAny = true;
            continue;
        }

        String source;
        String slotError;
        if (!_readScriptFile(path, &source, &slotError)) {
            clearRuntimeAnimationScript(slot);
            if (error && error->length() == 0) {
                *error = String("slot ") + slot + ": " + slotError;
            }
            continue;
        }

        if (isNoScriptStubSource(source)) {
            source = defaultSource;
            if (!_writeScriptFile(path, source, &slotError)) {
                clearRuntimeAnimationScript(slot);
                if (error && error->length() == 0) {
                    *error = String("slot ") + slot + ": " + slotError;
                }
                continue;
            }
        }

        if (!_parseAndInstall(source, &slotError, slot)) {
            clearRuntimeAnimationScript(slot);
            if (error && error->length() == 0) {
                *error = String("slot ") + slot + ": " + slotError;
            }
            continue;
        }
        if (shouldSeedBuiltin) {
            markBuiltinBankSlotSeeded(*builtin);
        }
        loadedAny = true;
    }

    return loadedAny || !error || error->length() == 0;
}

bool RuntimeScriptManager::_validBankSlot(uint8_t slot) const {
    return slot >= 1 && slot <= ANIMATION_BANK_SLOT_COUNT;
}

String RuntimeScriptManager::_bankSlotPath(uint8_t slot) const {
    return String("/bank_slot") + String(slot) + ".anim";
}

String RuntimeScriptManager::_normalizePath(const char* fileName) const {
    if (!fileName || fileName[0] == '\0') {
        return String();
    }

    String path(fileName);
    path.trim();
    if (path.length() == 0) {
        return String();
    }
    if (!path.endsWith(".game")) {
        path += ".game";
    }
    if (!path.startsWith("/")) {
        path = "/games/" + path;
    }
    return path;
}

bool RuntimeScriptManager::_parseAndInstall(const String& source, String* error, int forcedScriptId) {
    if (source.length() == 0) {
        return setError(error, "script source is empty");
    }

    AnimationStep* tempSteps = g_parseScratchSteps;
    auto fail = [&](const String& message) {
        return setError(error, message);
    };

    ScriptLabel labels[MAX_SCRIPT_LABELS];
    PendingGoto gotos[MAX_SCRIPT_GOTOS];
    uint8_t labelCount = 0;
    uint8_t gotoCount = 0;
    uint8_t stepCount = 0;
    int16_t currentStep = -1;
    uint32_t currentDisplayMask = 0;  // 0 => all displays
    long scriptId = -1;
    String scriptName;

    size_t lineStart = 0;
    uint16_t lineNumber = 0;
    while (lineStart <= source.length()) {
        size_t lineEnd = source.indexOf('\n', lineStart);
        if (lineEnd == (size_t)-1) {
            lineEnd = source.length();
        }
        String rawLine = source.substring(lineStart, lineEnd);
        lineStart = lineEnd + 1;
        lineNumber++;

        rawLine.replace("\r", "");
        if (rawLine.length() >= 192) {
            return fail(String("line ") + lineNumber + ": line too long");
        }

        char lineBuf[192];
        rawLine.toCharArray(lineBuf, sizeof(lineBuf));
        stripComments(lineBuf);
        trimLine(lineBuf);
        if (lineBuf[0] == '\0') {
            if (lineStart > source.length()) break;
            continue;
        }

        char tokens[MAX_TOKENS_PER_LINE][MAX_TOKEN_LEN + 1];
        uint8_t tokenCount = 0;
        String tokenError;
        if (!tokenizeLine(lineBuf, tokens, &tokenCount, &tokenError)) {
            return fail(String("line ") + lineNumber + ": " + tokenError);
        }
        if (tokenCount == 0) {
            if (lineStart > source.length()) break;
            continue;
        }

        String command(tokens[0]);
        command.toLowerCase();

        if (command == "id") {
            long value = 0;
            if (tokenCount < 2 || !parseLongToken(tokens[1], &value) || value <= 0 || value > 255) {
                return fail(String("line ") + lineNumber + ": invalid script id");
            }
            scriptId = value;
        } else if (command == "name") {
            if (tokenCount < 2) {
                return fail(String("line ") + lineNumber + ": missing script name");
            }
            scriptName = joinTokens(tokens, 1, tokenCount);
            if (scriptName.length() > MAX_NAME_LEN) {
                return fail(String("line ") + lineNumber + ": script name too long");
            }
        } else if (command == "display") {
            if (currentStep >= 0) {
                return fail(String("line ") + lineNumber + ": display context must be set outside a step");
            }
            uint32_t parsedMask = 0;
            if (tokenCount < 2 || !parseDisplayMaskToken(tokens[1], &parsedMask)) {
                return fail(String("line ") + lineNumber + ": invalid display selector (use 1.." + String(NUM_DISPLAYS) + " or 'all')");
            }
            currentDisplayMask = parsedMask;
        } else if (command == "step") {
            if (stepCount >= MAX_SCRIPT_STEPS) {
                return fail(String("line ") + lineNumber + ": too many steps");
            }
            currentStep = stepCount;
            tempSteps[stepCount] = AnimationStep();
            tempSteps[stepCount].targetDisplayMask = currentDisplayMask;
            stepCount++;

            if (tokenCount >= 2) {
                if (labelCount >= MAX_SCRIPT_LABELS) {
                    return fail(String("line ") + lineNumber + ": too many labels");
                }
                for (uint8_t i = 0; i < labelCount; ++i) {
                    if (strcmp(labels[i].name, tokens[1]) == 0) {
                        return fail(String("line ") + lineNumber + ": duplicate label '" + String(tokens[1]) + "'");
                    }
                }
                strncpy(labels[labelCount].name, tokens[1], sizeof(labels[labelCount].name) - 1);
                labels[labelCount].name[sizeof(labels[labelCount].name) - 1] = '\0';
                labels[labelCount].stepIndex = (uint8_t)currentStep;
                labelCount++;
            }
        } else if (command == "end") {
            currentStep = -1;
        } else {
            if (currentStep < 0) {
                return fail(String("line ") + lineNumber + ": command outside step");
            }

            AnimationStep& step = tempSteps[currentStep];
            if (command == "wait") {
                long value = 0;
                if (tokenCount < 2 || !parseLongToken(tokens[1], &value) || value < 0) {
                    return fail(String("line ") + lineNumber + ": invalid wait value");
                }
                step.waitMs = (unsigned long)value;
            } else if (command == "mode") {
                DisplayMode mode;
                if (tokenCount < 2 || !parseModeToken(tokens[1], &mode)) {
                    return fail(String("line ") + lineNumber + ": invalid mode");
                }
                step.setMode = true;
                step.mode = mode;
            } else if (command == "text") {
                bool enabled = false;
                if (tokenCount < 2 || !parseTextEnabledToken(tokens[1], &enabled)) {
                    return fail(String("line ") + lineNumber + ": invalid text state");
                }
                step.setTextEnabled = true;
                step.textEnabled = enabled;
            } else if (command == "particles") {
                bool enabled = false;
                if (tokenCount < 2 || !parseBoolToken(tokens[1], &enabled)) {
                    return fail(String("line ") + lineNumber + ": invalid particles state");
                }
                step.setParticlesEnabled = true;
                step.particlesEnabled = enabled;
            } else if (command == "text_to_particles") {
                step.doTextToParticles = true;
            } else if (command == "screen_to_particles") {
                step.doScreenToParticles = true;
            } else if (command == "physics") {
                if (tokenCount < 2) {
                    return fail(String("line ") + lineNumber + ": invalid physics state");
                }
                String value(tokens[1]);
                value.toLowerCase();
                step.setPhysicsPaused = true;
                if (value == "paused" || value == "pause") {
                    step.physicsPaused = true;
                } else if (value == "running" || value == "run") {
                    step.physicsPaused = false;
                } else {
                    bool paused = false;
                    if (!parseBoolToken(tokens[1], &paused)) {
                        return fail(String("line ") + lineNumber + ": invalid physics state");
                    }
                    step.physicsPaused = paused;
                }
            } else if (command == "replace_particle_config") {
                bool enabled = false;
                if (tokenCount < 2 || !parseBoolToken(tokens[1], &enabled)) {
                    return fail(String("line ") + lineNumber + ": invalid replace_particle_config state");
                }
                step.replaceParticleConfig = enabled;
            } else if (command == "particle") {
                String parseError;
                if (tokenCount < 3 || !setParticleField(step, tokens[1], tokens[2], &parseError)) {
                    return fail(String("line ") + lineNumber + ": " + parseError);
                }
            } else if (command == "goto") {
                if (tokenCount < 2) {
                    return fail(String("line ") + lineNumber + ": goto requires a label");
                }
                if (gotoCount >= MAX_SCRIPT_GOTOS) {
                    return fail(String("line ") + lineNumber + ": too many goto statements");
                }
                strncpy(gotos[gotoCount].label, tokens[1], sizeof(gotos[gotoCount].label) - 1);
                gotos[gotoCount].label[sizeof(gotos[gotoCount].label) - 1] = '\0';
                gotos[gotoCount].stepIndex = (uint8_t)currentStep;
                gotoCount++;
                step.gotoRepeat = -1;
                if (tokenCount >= 3) {
                    String mode(tokens[2]);
                    mode.toLowerCase();
                    if (mode == "forever") {
                        step.gotoRepeat = -1;
                    } else if (mode == "repeat") {
                        long value = 0;
                        if (tokenCount < 4 || !parseLongToken(tokens[3], &value)) {
                            return fail(String("line ") + lineNumber + ": goto repeat requires a count");
                        }
                        step.gotoRepeat = (int16_t)value;
                    } else {
                        return fail(String("line ") + lineNumber + ": goto expects 'repeat N' or 'forever'");
                    }
                }
            } else {
                return fail(String("line ") + lineNumber + ": unknown command '" + String(tokens[0]) + "'");
            }
        }

        if (lineStart > source.length()) break;
    }

    if (scriptId <= 0 && forcedScriptId <= 0) {
        return fail("missing script id");
    }
    if (stepCount == 0) {
        return fail("script has no steps");
    }
    uint8_t finalScriptId = (forcedScriptId > 0) ? (uint8_t)forcedScriptId : (uint8_t)scriptId;
    if (scriptName.length() == 0) {
        scriptName = String("slot_") + (int)finalScriptId;
    }

    for (uint8_t i = 0; i < gotoCount; ++i) {
        bool found = false;
        for (uint8_t j = 0; j < labelCount; ++j) {
            if (strcmp(gotos[i].label, labels[j].name) == 0) {
                tempSteps[gotos[i].stepIndex].gotoStep = labels[j].stepIndex;
                found = true;
                break;
            }
        }
        if (!found) {
            return fail(String("unknown goto label '") + gotos[i].label + "'");
        }
    }

    AnimationStep* finalSteps = new (std::nothrow) AnimationStep[stepCount];
    if (!finalSteps) {
        return fail("out of memory allocating script steps");
    }
    for (uint8_t i = 0; i < stepCount; ++i) {
        finalSteps[i] = tempSteps[i];
    }

    char* finalName = dupCString(scriptName);
    if (!finalName) {
        delete[] finalSteps;
        return setError(error, "out of memory allocating script name");
    }

    if (!installRuntimeAnimationScript(finalScriptId, finalName, finalSteps, stepCount)) {
        delete[] finalName;
        delete[] finalSteps;
        return setError(error, "runtime script registry is full");
    }

    _lastInstalledId = finalScriptId;
    _lastInstalledName = scriptName;
    return true;
}
