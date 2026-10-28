#pragma once

// Shared bounds for the extended v83 table. Existing and Evan action IDs stay
// fixed; only the two original modern standing actions are appended.
namespace AvatarActionIds {
constexpr int NativeCount=162;
constexpr int EvanCount=5;
constexpr int FloatingStand1=167, FloatingStand2=168;
constexpr int Count=169;
}
