---
phase: quick
plan: 01
type: execute
wave: 1
depends_on: []
files-modified:
  - source/ui/ui.h
  - source/ui/ui.cpp
  - source/main.cpp
autonomous: true
requirements: []
must_haves:
  truths:
    - "A Close button exists in the UI that cleanly shuts down the application"
  artifacts:
    - path: "source/ui/ui.h"
      provides: "Close button member declaration"
    - path: "source/ui/ui.cpp"
      provides: "Close button widget and callback implementation"
    - path: "source/main.cpp"
      provides: "Application shutdown logic for close button"
  key_links:
    - from: "btn_close_app"
      to: "close_app_callback"
      via: "FL_METHOD_CALLBACK_1"
---

# Quick Task Plan: Add Close Application Button

## Objective
Add a Close Application button to the FLTK UI that cleanly shuts down the application, properly stopping any running encode sessions.

## Tasks

### Task 1: Add close button declaration to ui.h
- **files:** `source/ui/ui.h`
- **action:** Add `Fl_Button* btn_close_app;` member declaration to the `user_interface` class. Also add `close_app` method declaration.
- **verify:** `grep -q "btn_close_app" source/ui/ui.h && grep -q "close_app" source/ui/ui.h`
- **done:** Header file contains the button pointer and method declaration.

### Task 2: Implement close button in ui.cpp
- **files:** `source/ui/ui.cpp`
- **action:** 
  1. In the constructor, create the close button widget (add after btn_stop_encode around line 268)
  2. Implement `close_app` method that calls `Fl::quit()` to exit the FLTK event loop
  3. Bind the button callback in `init_ui_callbacks` using FL_METHOD_CALLBACK_1
- **verify:** `grep -q "btn_close_app" source/ui/ui.cpp && grep -q "close_app" source/ui/ui.cpp`
- **done:** UI contains a functional close button wired to the close_app method.

### Task 3: Wire close button callback in main.cpp
- **files:** `source/main.cpp`
- **action:** Add close_app function that stops any running encode session before exiting, and pass it to init_ui_callbacks.
- **verify:** `grep -q "close_app" source/main.cpp`
- **done:** Main has a close_app function that properly stops encoding before exit.