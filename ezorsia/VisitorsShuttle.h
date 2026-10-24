#pragma once
#include <windows.h>

namespace VisitorsShuttle {
bool IsSkill(int skill);
int SkillForJob(int job);
bool Install(HMODULE module);
}
