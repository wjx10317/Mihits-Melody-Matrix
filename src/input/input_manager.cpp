#include "input/input_manager.h"
#include "util/logger.h"

namespace melody_matrix::input {

bool InputManager::processEvent(const SDL_Event& event, int64_t audioTimeMs) {
    switch (event.type) {
    case SDL_KEYDOWN: {
        int32_t key = event.key.keysym.sym;

        // Ignore key repeats
        if (event.key.repeat) return false;

        m_keyStates[key] = true;

        if (onKeyPress) {
            KeyEvent ke;
            ke.timestamp = audioTimeMs;
            ke.keyCode = key;
            ke.pressed = true;
            onKeyPress(ke);
        }
        return true;
    }

    case SDL_KEYUP: {
        int32_t key = event.key.keysym.sym;
        m_keyStates[key] = false;

        if (onKeyRelease) {
            KeyEvent ke;
            ke.timestamp = audioTimeMs;
            ke.keyCode = key;
            ke.pressed = false;
            onKeyRelease(ke);
        }
        return true;
    }

    default:
        return false;
    }
}

bool InputManager::isKeyDown(int32_t keyCode) const {
    auto it = m_keyStates.find(keyCode);
    return it != m_keyStates.end() && it->second;
}

void InputManager::reset() {
    m_keyStates.clear();
}

} // namespace input
