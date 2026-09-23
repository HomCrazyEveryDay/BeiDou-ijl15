#pragma once
#include <cstring>

namespace EvanMountRender {
// Preserve the requested climbing pose for Evan. Native 083/084 riding
// code changes BOTH ladder (36) and rope (37) to rope. User requested
// distinct, correct climbing poses; leave other mounts/actions untouched.
inline int BodyAction(DWORD model, int requested, int selected) {
    return model >= 1902040 && model <= 1902042 &&
        (requested == 36 || requested == 37) ? requested : selected;
}
// v83 4138BE and v84 41434D both load category-190 mounts from
// appearance[18], not the buff model argument. Our server derives Evan's
// model from his saddle; bridge that model into a private rendering copy.
// This does not create an inventory item or alter the character appearance.
inline bool Prepare(DWORD model, const DWORD* appearance, DWORD* copy) {
    if (!appearance || model < 1902040 || model > 1902042 ||
        appearance[19] != 1912033 + (model - 1902040)) return false;
    std::memcpy(copy, appearance, 0xD0);
    copy[18] = model;
    return true;
}
}
