# Quick Task Summary: Fix UI log append thread safety (Tech Debt 5)

## Completed

Added `Fl::lock()` / `Fl::unlock()` / `Fl::awake()` to `encode_log_append()` in `source/ui/ui.cpp`, matching the existing pattern in `transport_log_append()`.

## Files Modified

- `source/ui/ui.cpp` — Added thread-safety locking to `encode_log_append()` (lines 457-463)

## Summary

The `encode_log_append()` method was called from background threads but modified the FLTK `Fl_Text_Display` widget without calling `Fl::lock()` / `Fl::unlock()` / `Fl::awake()`. This could cause crashes, corrupted text display, or frozen UI due to FLTK not being thread-safe. The fix adds the same three calls that `transport_log_append()` already uses, making both log append methods consistent and thread-safe.
