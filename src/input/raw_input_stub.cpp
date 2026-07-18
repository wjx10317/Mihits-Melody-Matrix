/**
 * @file raw_input_stub.cpp
 * @brief 非 Windows：Raw 适配器不可用
 */
#ifndef _WIN32

#include "input/gameplay_input.h"

namespace melody_matrix::input {

std::unique_ptr<IGameplayInput> tryMakeRawInputAdapter() {
    return nullptr;
}

} // namespace melody_matrix::input

#endif
