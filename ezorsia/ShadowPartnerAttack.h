#pragma once

namespace ShadowPartnerAttack {
// Only packets with the complete native copied half are eligible for synchronization.
inline int CopiedAttackCount(int skill, int lines) {
    int originalLines = 0;
    switch (skill) {
    case 0:
    case 4101005: // Drain
    case 4111004: // Shadow Meso, only if the client actually creates copied hits
    case 4111005: // Avenger
    case 4121008: // Ninja Storm
        originalLines = 1;
        break;
    case 4001344: // Lucky Seven
        originalLines = 2;
        break;
    case 4121007: // Triple Throw
        originalLines = 3;
        break;
    }
    return originalLines > 0 && lines == originalLines * 2 ? originalLines : 0;
}
}
