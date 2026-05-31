#pragma once

#include <algorithm>
#include <functional>
#include <string>
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
  Fl_Flex* mpegts_options_group;
  Fl_Input* input_listen_port;
  Fl_Button* btn_preview_input;
  Fl_Choice* choice_codec;
  static Fl_Menu_Item menu_choice_codec[];
  Fl_Choice* choice_encoder;
  static Fl_Menu_Item menu_choice_encoder[];
  Fl_Input* input_encode_bitrate;
  Fl_Input* input_rist_address;
  Fl_Button* btn_start_encode;
  Fl_Button* btn_stop_encode;
  // Receiver / restream control section
  Fl_Flex* flx_receiver;
  Fl_Check_Button* check_receiver_enabled;
  Fl_Input* input_control_address;
  Fl_Input* input_control_token;
  Fl_Check_Button* check_reencode;
  Fl_Choice* choice_reencode_codec;
  Fl_Choice* choice_reencode_encoder;
  Fl_Input* input_reencode_bitrate;
  Fl_Check_Button* check_upscale;
  Fl_Multiline_Input* input_destinations;
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
                         FuncPtr start_funcptr,
                         FuncPtr stop_funcptr,
                         FuncPtr ndi_refresh_funcptr,
                         FuncPtr input_rist_address_funcptr,
                         FuncPtr preview_src_funcptr,
                         FuncPtr scaling_source_changed_funcptr);
  void transport_log_append(const std::string& msg) const;
  void encode_log_append(const std::string& msg) const;
  void init_ui();
  int run_ui();
  void add_ndi_choices(const std::vector<std::string>& choice_names);
  void clear_ndi_choices();
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
                             FuncPtr refresh_ndi_funcptr);
  void input_listen_port_cb(input_config* input_config);
  void input_rist_address_cb(output_config* output_config,
                             FuncPtr input_rist_address_funcptr);
  void select_codec(encode_config* encode_config);
  void select_encoder(encode_config* encode_config);
  void select_bitrate_source(encode_config* encode_config,
                             FuncPtr scaling_source_changed_funcptr);
  void encode_bitrate_cb(encode_config* encode_config);
  // Receiver / restream control callbacks
  void receiver_enabled_cb(receiver_control_config* receiver_config);
  void receiver_address_cb(receiver_control_config* receiver_config);
  void receiver_token_cb(receiver_control_config* receiver_config);
  void receiver_reencode_cb(receiver_control_config* receiver_config);
  void receiver_codec_cb(receiver_control_config* receiver_config);
  void receiver_encoder_cb(receiver_control_config* receiver_config);
  void receiver_bitrate_cb(receiver_control_config* receiver_config);
  void receiver_upscale_cb(receiver_control_config* receiver_config);
  void receiver_destinations_cb(receiver_control_config* receiver_config);
  void start(FuncPtr start_funcptr);
  void stop(FuncPtr stop_funcptr);
  void refresh_ndi_devices(FuncPtr refresh_ndi_funcptr);
  void btn_preview_input_cb(FuncPtr preview_src_funcptr);
};