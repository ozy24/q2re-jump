// [Jump] Single source of truth for the mod's version number.
//
// Keep MAJOR/MINOR/PATCH in sync with the root VERSION file.
// Bump only when cutting a tagged release (see docs/release-process.md).
#pragma once

#define JUMP_VERSION_MAJOR 0
#define JUMP_VERSION_MINOR 3
#define JUMP_VERSION_PATCH 0

#define JUMP_VER_STR_HELPER(a, b, c) #a "." #b "." #c
#define JUMP_VER_STR(a, b, c) JUMP_VER_STR_HELPER(a, b, c)
#define JUMP_VERSION_STRING JUMP_VER_STR(JUMP_VERSION_MAJOR, JUMP_VERSION_MINOR, JUMP_VERSION_PATCH)
