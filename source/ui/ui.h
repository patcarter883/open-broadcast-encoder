// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Flex.H>
#include <FL/Fl_Text_Buffer.H>
#include <stdint.h>

#include "lib/lib.h"
// The widget tree comes from the design file: ui.fld is turned into
// ui_widgets.{h,cpp} by fluid at build time, and declares every panel, row,
// label, output and button as a public member of `ui_widgets`, plus the static
// Fl_Menu_Item arrays and the menu pointers. This header adds the behaviour.
//
// Editing the layout means editing ui.fld in fluid -- ui_widgets.h and
// ui_widgets.cpp are regenerated on every build and are not in version control.
#include "ui/ui_widgets.h"

// user_interface is the generated widget tree plus everything the design file
// cannot express: the callbacks, the thread-safe setters, the log buffers and
// the model pointers. The generated constructor runs first (base class), so by
// the time this class's constructor body runs every widget already exists.
class user_interface : public ui_widgets
{
public:
  user_interface();
  // Shows the window and then walks the flex tree once, so the layout the
  // design file describes is what is actually on screen.
  void show(int argc, char** argv);
  void layout();
  void init_ui_callbacks(input_config* input_c,
                         encode_config* encode_c,
                         output_config* output_c,
                         receiver_control_config* receiver_c,
                         bridge_control_config* bridge_c,
                         hosted_config* hosted_c,
                         FuncPtr start_funcptr,
                         FuncPtr stop_funcptr,
                         FuncPtr ndi_refresh_funcptr,
                         FuncPtr input_rist_address_funcptr,
                         FuncPtr preview_src_funcptr,
                         FuncPtr scaling_source_changed_funcptr,
                         FuncPtr save_settings_funcptr,
                         FuncPtr bridge_find_funcptr,
                         FuncPtr bridge_claim_funcptr,
                         FuncPtr bridge_apply_funcptr,
                         FuncPtr bridge_calibrate_funcptr,
                         FuncPtr hosted_allocate_funcptr,
                         FuncPtr hosted_signin_funcptr,
                         FuncPtr hosted_signout_funcptr,
                         FuncPtr refresh_capture_funcptr);
  // Push persisted configs into the widgets after settings::load(). Call once,
  // on the main thread, after init_ui_callbacks() and before show().
  void apply_settings(const input_config& input_c,
                      const encode_config& encode_c,
                      const output_config& output_c,
                      const receiver_control_config& receiver_c,
                      const bridge_control_config& bridge_c,
                      const hosted_config& hosted_c);
  // Show the encode lifecycle state. Safe to call from a non-UI thread: takes
  // the FLTK lock for the widget write and, when is_failed, presents the
  // buttons as stopped (Start available, Stop inactive) because the send loop
  // has already ended.
  void set_encode_state(const std::string& text, bool is_failed);
  // Bridge state. Safe to call from a background thread: takes the FLTK lock.
  // set_bridge_discovered updates the model AND the widgets, so a worker never
  // touches the config directly. set_bridge_token never displays the token --
  // the pane only ever says whether one is held.
  void set_bridge_discovered(const std::string& uid,
                             const std::string& address,
                             const std::string& text,
                             bool is_error);
  void set_bridge_token(const std::string& token);
  void set_bridge_message(const std::string& text, bool is_error);
  // The calibration report, and the DT-28 enable rule. Both take the FLTK lock.
  void set_bridge_calibration(const std::string& text, bool is_error);
  void set_bridge_calibrate_enabled(bool on);
  // Hosted sign-in state. Same rules: takes the FLTK lock, so it is safe from a
  // background thread, and the token is never rendered -- only set/not-set.
  void set_hosted_state(const std::string& text, bool is_error);
  void set_hosted_token(const std::string& token);
  // The LAN cameras found by the capture browse (MC4). Runs on the same kind of
  // tracked background thread as the bridge actions, so it takes the FLTK lock
  // and repopulates the picker from a worker without touching the config.
  // `sources` is (label, address) pairs -- the operator selects by NAME, the
  // address is what the reader binds toward. `message` reports the browse
  // outcome (found N, or an error).
  void set_capture_sources(
      const std::vector<std::pair<std::string, std::string>>& sources,
      const std::string& message,
      bool is_error);
  // Where the encoder should SEND, decided by an allocation (DT-20.1): the
  // bridge's listen URL when the portal routed through one, else the node's
  // rist_url. Takes the FLTK lock and updates the model and the widget
  // together, like the bridge setters -- a worker must never write the config
  // directly.
  void set_encoder_target(const std::string& url);
  // The ids the token was minted against, and the backplane's row for the
  // bridge this encoder is driving. Written together because they are learned
  // at different times and a partial update would zero the other.
  void set_hosted_ids(long device_id, long bridge_id);
  void transport_log_append(const std::string& msg) const;
  void encode_log_append(const std::string& msg) const;
  void init_ui();
  int run_ui();
  void add_ndi_choices(const std::vector<std::string>& choice_names);
  void clear_ndi_choices();
  // Capture (MC4) picker callbacks, mirroring the NDI picker. The chooser
  // writes the selected camera's ADDRESS to the model's capture_address (on the
  // FLTK thread, from the menu item's user_data); the browse worker goes
  // through set_capture_sources instead.
  void choose_capture_input(input_config* input_config);
  void refresh_capture(FuncPtr refresh_capture_funcptr);
  // Stable storage backing the capture picker's labels and the addresses held
  // in each item's user_data, so both outlive the Fl_Choice's items.
  std::vector<std::string> capture_label_storage;
  std::vector<std::string> capture_address_storage;
  void lock();
  void unlock();

private:
  Fl_Text_Buffer transport_log_buffer;
  Fl_Text_Buffer encode_log_buffer;
  // Owns the storage backing the user_data pointers attached to NDI
  // Fl_Choice items so they remain valid for the lifetime of the choice.
  std::vector<std::string> ndi_choice_storage;
  void choose_ndi_input(input_config* input_config);
  void choose_input_protocol(input_config* input_config,
                             FuncPtr refresh_ndi_funcptr,
                             FuncPtr refresh_capture_funcptr);
  void input_listen_port_cb(input_config* input_config);
  void input_rist_address_cb(output_config* output_config,
                             FuncPtr input_rist_address_funcptr);
  void select_codec(encode_config* encode_config);
  void select_encoder(encode_config* encode_config);
  void select_bitrate_source(encode_config* encode_config,
                             FuncPtr scaling_source_changed_funcptr);
  void encode_bitrate_cb(encode_config* encode_config);
  void mpegts_alignment_cb(encode_config* encode_config);
  // Receiver / restream control callbacks
  void receiver_enabled_cb(receiver_control_config* receiver_config);
  void receiver_address_cb(receiver_control_config* receiver_config);
  void receiver_token_cb(receiver_control_config* receiver_config);
  void receiver_destinations_cb(receiver_control_config* receiver_config);
  // Bridge (LAN) control callbacks
  void bridge_address_cb(bridge_control_config* bridge_config);
  void bridge_listen_cb(bridge_control_config* bridge_config);
  void bridge_forward_cb(bridge_control_config* bridge_config);
  void bridge_interface_cb(bridge_control_config* bridge_config);
  void bridge_find(FuncPtr find_funcptr);
  void bridge_claim(FuncPtr claim_funcptr);
  void bridge_apply(FuncPtr apply_funcptr);
  void bridge_calibrate(FuncPtr calibrate_funcptr);
  // The model the bridge widgets edit. Held as a pointer (set once in
  // init_ui_callbacks) so the thread-safe setters can update the model as well
  // as the widgets -- a worker thread must never write the config directly.
  bridge_control_config* bridge_config_ptr = nullptr;
  // Hosted control-plane callbacks
  void hosted_url_cb(hosted_config* hosted_config);
  void hosted_allocate(FuncPtr allocate_funcptr);
  void hosted_sign_in(FuncPtr signin_funcptr);
  void hosted_sign_out(FuncPtr signout_funcptr);
  // The model the hosted widgets edit; set once in init_ui_callbacks, so the
  // thread-safe setters update the model as well as the widgets.
  hosted_config* hosted_config_ptr = nullptr;
  // The model behind the RIST output address, held for the same reason: an
  // allocation can move the encoder's target, and that write must not happen on
  // a worker thread.
  output_config* output_config_ptr = nullptr;
  void start(FuncPtr start_funcptr);
  void stop(FuncPtr stop_funcptr);
  void save_settings(FuncPtr save_settings_funcptr);
  void refresh_ndi_devices(FuncPtr refresh_ndi_funcptr);
  void btn_preview_input_cb(FuncPtr preview_src_funcptr);
};
