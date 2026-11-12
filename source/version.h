// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

// One definition of the product version, set by CMake from project(VERSION).
//
// It appears in three places an operator or a support thread looks: the window
// title, `--version`, and the release artefact names. The fallback keeps a
// translation unit that is compiled outside CMake (an editor's language server)
// building, and says plainly that it is not a release build.

#ifndef OBC_VERSION
#  define OBC_VERSION "dev"
#endif
