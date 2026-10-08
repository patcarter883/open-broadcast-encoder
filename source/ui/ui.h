// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Flex.H>
#include <FL/Fl_Grid.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Text_Display.H>
#include <stdint.h>

#include "FL/fl_callback_macros.H"
#include "lib/lib.h"

using FuncPtr = void (*)();

class user_interface
{
public:
  user_interface();
  Fl_Double_Window* main_window;
  Fl_Flex* pack;
  Fl_Flex* flx_top;
  Fl_Flex* flx_input;
  Fl_Choice* choice_input_protocol;
  static Fl_Menu_Item menu_choice_input_protocol[];
  static Fl_Menu_Item* select_test_input;
  static Fl_Menu_Item* select_sdp_input;
  static Fl_Menu_Item* select_ndi_input;
  static Fl_Menu_Item* select_mpegts_input;
  Fl_Flex* sdp_options_group;
  Fl_Button* btn_open_sdp;
  Fl_Flex* ndi_options_group;
  Fl_Choice* choice_ndi_input;
  Fl_Button* btn_refresh_ndi_devices;
  // JPEG XS capture (MC4): the LAN cameras found over mDNS (_obr-cam._udp),
  // shown as a picker the operator selects by name. The listen port is the same
  // field the MPEG-TS/raw_local modes use (the stream port to receive on).
  Fl_Flex* capture_options_group;
  Fl_Choice* choice_capture_input;
  Fl_Button* btn_refresh_capture;
  Fl_Output* capture_state_output;
  Fl_Flex* mpegts_options_group;
  Fl_Input* input_listen_port;
  Fl_Button* btn_preview_input;
  Fl_Choice* choice_codec;
  static Fl_Menu_Item menu_choice_codec[];
  Fl_Choice* choice_encoder;
  static Fl_Menu_Item menu_choice_encoder[];
  Fl_Input* input_encode_bitrate;
  Fl_Input* input_mpegts_alignment;
  Fl_Input* input_rist_address;
  // Lifecycle of the encode pipeline (Idle / Starting / Streaming / FAILED).
  // Written from the send thread via set_encode_state, which takes the FLTK
  // lock; a pipeline or RIST-send failure is otherwise indistinguishable from a
  // healthy stream.
  Fl_Output* encode_state_output;
  Fl_Button* btn_start_encode;
  Fl_Button* btn_stop_encode;
  Fl_Button* btn_save_settings;
  Fl_Button* btn_exit;
  // Receiver / restream control section
  Fl_Flex* flx_receiver;
  Fl_Check_Button* check_receiver_enabled;
  Fl_Input* input_control_address;
  Fl_Input* input_control_token;
  Fl_Multiline_Input* input_destinations;
  // Bridge (LAN) control section (DT-19, DT-21). The encoder finds the bridge,
  // claims it once, then applies the portal's desired state. The pair token is
  // never displayed -- bridge_token_output reports only whether one is held.
  Fl_Flex* flx_bridge;
  Fl_Input* input_bridge_address;
  Fl_Input* input_bridge_listen;
  Fl_Input* input_bridge_forward;
  Fl_Input* input_bridge_interface;
  Fl_Output* bridge_state_output;
  Fl_Output* bridge_token_output;
  Fl_Button* btn_bridge_find;
  Fl_Button* btn_bridge_claim;
  Fl_Button* btn_bridge_apply;
  // Hosted control plane (BACKPLANE §2). Until the encoder is signed in it is
  // self-host-only: no device token means the portal half has no runtime path.
  // The device token is never displayed -- hosted_token_output says only
  // whether one is held.
  Fl_Flex* flx_hosted;
  Fl_Input* input_backplane_url;
  Fl_Output* hosted_state_output;
  Fl_Output* hosted_token_output;
  Fl_Button* btn_hosted_signin;
  Fl_Button* btn_hosted_signout;
  // One Allocate action (DT-20.1): allocate the hosted session, then apply the
  // bridge the portal chose and report it. One button because the bridge's
  // upstream IS the node the allocator picks.
  Fl_Button* btn_hosted_allocate;
  Fl_Grid* grid_stats;
  Fl_Output* bandwidth_output;
  Fl_Output* link_quality_output;
  Fl_Output* retransmitted_packets_output;
  Fl_Output* rtt_output;
  Fl_Output* total_packets_output;
  Fl_Output* encode_bitrate_output;
  Fl_Output* cumulative_bandwidth_output;
  Fl_Output* cumulative_retransmitted_packets_output;
  Fl_Output* cumulative_total_packets_output;
  Fl_Output* cumulative_encode_bitrate_output;
  Fl_Flex* flx_wan_stats;
  Fl_Output* wan_quality_output;
  Fl_Output* wan_rtt_output;
  Fl_Choice* choice_bitrate_source;
  static Fl_Menu_Item menu_choice_bitrate_source[];
  Fl_Flex* flx_bottom;
  Fl_Text_Display* transport_log_display;
  Fl_Text_Display* encode_log_display;
  void show(int argc, char** argv) const;
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