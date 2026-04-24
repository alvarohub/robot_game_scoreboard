#include "Animations.h"

#include <string.h>

#include "generated/GameScripts.generated.h"

namespace {

static constexpr uint8_t MAX_RUNTIME_ANIMATION_SCRIPTS = ANIMATION_BANK_SLOT_COUNT;

struct RuntimeAnimationScriptSlot {
    bool inUse = false;
    AnimationScript script;
    char* ownedName = nullptr;
    AnimationStep* ownedSteps = nullptr;
};

RuntimeAnimationScriptSlot g_runtimeScripts[MAX_RUNTIME_ANIMATION_SCRIPTS];

void freeRuntimeScriptSlot(RuntimeAnimationScriptSlot& slot) {
    delete[] slot.ownedName;
    delete[] slot.ownedSteps;
    slot.inUse = false;
    slot.ownedName = nullptr;
    slot.ownedSteps = nullptr;
    slot.script = AnimationScript();
}

RuntimeAnimationScriptSlot* findRuntimeScriptSlot(uint8_t id) {
    for (uint8_t i = 0; i < MAX_RUNTIME_ANIMATION_SCRIPTS; ++i) {
        if (g_runtimeScripts[i].inUse && g_runtimeScripts[i].script.id == id) {
            return &g_runtimeScripts[i];
        }
    }
    return nullptr;
}

RuntimeAnimationScriptSlot* firstFreeRuntimeScriptSlot() {
    for (uint8_t i = 0; i < MAX_RUNTIME_ANIMATION_SCRIPTS; ++i) {
        if (!g_runtimeScripts[i].inUse) {
            return &g_runtimeScripts[i];
        }
    }
    return nullptr;
}

}  // namespace

bool installRuntimeAnimationScript(uint8_t id, char* name, AnimationStep* steps, uint8_t stepCount) {
    if (id == 0 || !name || !steps || stepCount == 0) {
        return false;
    }

    RuntimeAnimationScriptSlot* slot = findRuntimeScriptSlot(id);
    if (!slot) {
        slot = firstFreeRuntimeScriptSlot();
    }
    if (!slot) {
        return false;
    }

    freeRuntimeScriptSlot(*slot);
    slot->inUse = true;
    slot->ownedName = name;
    slot->ownedSteps = steps;
    slot->script.id = id;
    slot->script.name = slot->ownedName;
    slot->script.steps = slot->ownedSteps;
    slot->script.stepCount = stepCount;
    return true;
}

bool clearRuntimeAnimationScript(uint8_t id) {
    RuntimeAnimationScriptSlot* slot = findRuntimeScriptSlot(id);
    if (!slot) {
        return false;
    }
    freeRuntimeScriptSlot(*slot);
    return true;
}

const AnimationScript* findRuntimeAnimationScript(uint8_t id) {
    RuntimeAnimationScriptSlot* slot = findRuntimeScriptSlot(id);
    return slot ? &slot->script : nullptr;
}

void clearAllRuntimeAnimationScripts() {
    for (uint8_t i = 0; i < MAX_RUNTIME_ANIMATION_SCRIPTS; ++i) {
        freeRuntimeScriptSlot(g_runtimeScripts[i]);
    }
}

uint8_t runtimeAnimationScriptCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_RUNTIME_ANIMATION_SCRIPTS; ++i) {
        if (g_runtimeScripts[i].inUse) {
            count++;
        }
    }
    return count;
}

const AnimationScript* runtimeAnimationScriptAt(uint8_t index) {
    uint8_t current = 0;
    for (uint8_t i = 0; i < MAX_RUNTIME_ANIMATION_SCRIPTS; ++i) {
        if (!g_runtimeScripts[i].inUse) {
            continue;
        }
        if (current == index) {
            return &g_runtimeScripts[i].script;
        }
        current++;
    }
    return nullptr;
}

const AnimationScript* findAnimationScript(uint8_t id) {
    if (id >= 1 && id <= ANIMATION_BANK_SLOT_COUNT) {
        return findRuntimeAnimationScript(id);
    }

    RuntimeAnimationScriptSlot* runtimeSlot = findRuntimeScriptSlot(id);
    if (runtimeSlot) {
        return &runtimeSlot->script;
    }

    return GeneratedGameScripts::findGeneratedAnimationScript(id);
}
