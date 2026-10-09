// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "ui/ui.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Flex.H>
#include <FL/Fl_Grid.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Text_Display.H>
#include <stdint.h>

#include "FL/fl_callback_macros.H"

namespace
{
bool parse_proto(const std::string& s, output_proto& out)
{
  if (s == "rtmp") {
    out = output_proto::rtmp;
  } else if (s == "rtmps") {
    out = output_proto::rtmps;
  } else if (s == "srt") {
    out = output_proto::srt;
  } else if (s == "rist") {
    out = output_proto::rist;
  } else {
    return false;
  }
  return true;
}

// Parse the receiver CONTROL address "host:port" / "[ipv6]:port" / "host".
// Unlike parse_address() (RIST media, default port 5000), this defaults the
// port to the control-plane default the caller supplies (8080) and handles the
// bracketed IPv6 form.
void parse_control_address(const std::string& in, std::string& host, int& port)
{
  std::size_t colon = std::string::npos;
  const std::size_t bracket = in.rfind(']');
  if (bracket != std::string::npos) {  // [ipv6]:port
    host = in.substr(0, bracket + 1);
    colon = in.find(':', bracket);
  } else {
    colon = in.rfind(':');
    host = (colon != std::string::npos) ? in.substr(0, colon) : in;
  }
  if (colon != std::string::npos && colon + 1 < in.size()) {
    try {
      const int p = std::stoi(in.substr(colon + 1));
      if (p >= 1 && p <= 65535) {
        port = p;  // else keep the supplied default
      }
    } catch (...) {  // NOLINT(bugprone-empty-catch)
    }
  }
  if (host.empty()) {
    host = "127.0.0.1";
  }
}

// Parse the destinations text area: one destination per line,
// "<type> <url> [key...]" whitespace-separated. Invalid lines are skipped.
std::vector<receiver_destination> parse_destinations(const char* text)
{
  std::vector<receiver_destination> dests;
  if (text == nullptr) {
    return dests;
  }
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    std::istringstream line_stream(line);
    std::string type;
    std::string url;
    if (!(line_stream >> type >> url)) {
      continue;  // need at least a type and a url
    }
    std::string key;
    std::getline(line_stream, key);  // remainder = optional key/streamid
    const std::size_t begin = key.find_first_not_of(" \t");
    key = (begin == std::string::npos) ? std::string {} : key.substr(begin);

    receiver_destination d;
    if (!parse_proto(type, d.proto)) {
      continue;
    }
    d.url = url;
    d.stream_key = key;
    dests.push_back(std::move(d));
  }
  return dests;
}

// Select the menu item whose user_data encodes `value`. The encoder menu order
// (AMD, NVENC, QSV, Software) does NOT match the encoder enum order, so a saved
// choice must be restored by matching user_data — never by using the enum as a
// menu index.
void select_choice_by_userdata(Fl_Choice* choice, long value)
{
  const Fl_Menu_Item* menu = choice->menu();
  if (menu == nullptr) {
    return;
  }
  for (int i = 0; menu[i].text != nullptr; ++i) {
    if (reinterpret_cast<long>(menu[i].user_data()) == value) {
      choice->value(i);
      return;
    }
  }
}

const char* proto_label(output_proto p)
{
  switch (p) {
    case output_proto::rtmp:
      return "rtmp";
    case output_proto::rtmps:
      return "rtmps";
    case output_proto::srt:
      return "srt";
    case output_proto::rist:
      return "rist";
  }
  return "rtmp";
}

// Render destinations back into the one-per-line text the input area expects.
// Round-trips with parse_destinations() above.
std::string format_destinations(const std::vector<receiver_destination>& dests)
{
  std::string text;
  for (const auto& d : dests) {
    text += proto_label(d.proto);
    text += ' ';
    text += d.url;
    if (!d.stream_key.empty()) {
      text += ' ';
      text += d.stream_key;
    }
    text += '\n';
  }
  return text;
}
}  // namespace

Fl_Menu_Item user_interface::menu_choice_input_protocol[] = {
    {.text = "Test Source",
     .shortcut_ = 0,
     .callback_ = 0,
     .user_data_ = (void*)(0),
     .flags = 0,
     .labeltype_ = (uchar)FL_NORMAL_LABEL,
     .labelfont_ = 0,
     .labelsize_ = 14,
     .labelcolor_ = 0},
    {.text = "MPEGTS",
     .shortcut_ = 0,
     .callback_ = 0,
     .user_data_ = (void*)(1),
     .flags = 0,
     .labeltype_ = (uchar)FL_NORMAL_LABEL,
     .labelfont_ = 0,
     .labelsize_ = 14,
     .labelcolor_ = 0},
    {.text = "SDP / RTP",
     .shortcut_ = 0,
     .callback_ = 0,
     .user_data_ = (void*)(2),
     .flags = 0,
     .labeltype_ = (uchar)FL_NORMAL_LABEL,
     .labelfont_ = 0,
     .labelsize_ = 14,
     .labelcolor_ = 0},
    {.text = "NDI",
     .shortcut_ = 0,
     .callback_ = 0,
     .user_data_ = (void*)(3),
     .flags = 0,
     .labeltype_ = (uchar)FL_NORMAL_LABEL,
     .labelfont_ = 0,
     .labelsize_ = 14,
     .labelcolor_ = 0},
    {.text = "OBS Raw (TCP)",
     .shortcut_ = 0,
     .callback_ = 0,
     .user_data_ = (void*)(4),
     .flags = 0,
     .labeltype_ = (uchar)FL_NORMAL_LABEL,
     .labelfont_ = 0,
     .labelsize_ = 14,
     .labelcolor_ = 0},
    {.text = "JPEG XS Camera (LAN)",
     .shortcut_ = 0,
     .callback_ = 0,
     // MC4: index 5 mirrors input_mode::jpegxs_capture. Appended AFTER
     // raw_local and before the `none` terminator, so the existing indices 0..4
     // keep their meaning (the enum's rule).
     .user_data_ = (void*)(5),
     .flags = 0,
     .labeltype_ = (uchar)FL_NORMAL_LABEL,
     .labelfont_ = 0,
     .labelsize_ = 14,
     .labelcolor_ = 0},
    {.text = 0,
     .shortcut_ = 0,
     .callback_ = 0,
     .user_data_ = 0,
     .flags = 0,
     .labeltype_ = 0,
     .labelfont_ = 0,
     .labelsize_ = 0,
     .labelcolor_ = 0}};
Fl_Menu_Item* user_interface::select_test_input =
    user_interface::menu_choice_input_protocol + 0;
Fl_Menu_Item* user_interface::select_mpegts_input =
    user_interface::menu_choice_input_protocol + 1;
Fl_Menu_Item* user_interface::select_sdp_input =
    user_interface::menu_choice_input_protocol + 2;
Fl_Menu_Item* user_interface::select_ndi_input =
    user_interface::menu_choice_input_protocol + 3;

Fl_Menu_Item user_interface::menu_choice_codec[] = {
    {"H264",
     0,
     0,
     (void*)(static_cast<long>(codec::h264)),
     0,
     (uchar)FL_NORMAL_LABEL,
     0,
     14,
     0},
    {"H265",
     0,
     0,
     (void*)(static_cast<long>(codec::h265)),
     0,
     (uchar)FL_NORMAL_LABEL,
     0,
     14,
     0},
    {"AV1",
     0,
     0,
     (void*)(static_cast<long>(codec::av1)),
     0,
     (uchar)FL_NORMAL_LABEL,
     0,
     14,
     0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0}};

Fl_Menu_Item user_interface::menu_choice_bitrate_source[] = {
    {"Local",
     0,
     0,
     (void*)(static_cast<long>(bitrate_source::local)),
     0,
     (uchar)FL_NORMAL_LABEL,
     0,
     14,
     0},
    {"Remote (OOB)",
     0,
     0,
     (void*)(static_cast<long>(bitrate_source::remote_oob)),
     0,
     (uchar)FL_NORMAL_LABEL,
     0,
     14,
     0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0}};

Fl_Menu_Item user_interface::menu_choice_encoder[] = {
    {"AMD",
     0,
     0,
     (void*)(static_cast<long>(encoder::amd)),
     0,
     (uchar)FL_NORMAL_LABEL,
     0,
     14,
     0},
    {"NVENC",
     0,
     0,
     (void*)(static_cast<long>(encoder::nvenc)),
     0,
     (uchar)FL_NORMAL_LABEL,
     0,
     14,
     0},
    {"QSV",
     0,
     0,
     (void*)(static_cast<long>(encoder::qsv)),
     0,
     (uchar)FL_NORMAL_LABEL,
     0,
     14,
     0},
    {"Software",
     0,
     0,
     (void*)(static_cast<long>(encoder::software)),
     0,
     (uchar)FL_NORMAL_LABEL,
     0,
     14,
     0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0}};

user_interface::user_interface()
{
  {
    main_window = new Fl_Double_Window(1373, 847, "Open Broadcast Encoder");
    main_window->user_data((void*)(this));
    {
      pack = new Fl_Flex(0, 0, 1373, 847);
      {
        flx_top = new Fl_Flex(25, 25, 1323, 417);
        flx_top->type(1);
        {
          Fl_Flex* o = new Fl_Flex(25, 25, 433, 355);
          {
            flx_input = new Fl_Flex(25, 25, 433, 165, "Input");
            flx_input->box(FL_BORDER_BOX);
            {
              choice_input_protocol =
                  new Fl_Choice(31, 51, 421, 25, "Protocol");
              choice_input_protocol->down_box(FL_BORDER_BOX);
              choice_input_protocol->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              choice_input_protocol->when(FL_WHEN_RELEASE_ALWAYS);
              choice_input_protocol->menu(menu_choice_input_protocol);
            }  // Fl_Choice* choice_input_protocol
            {
              mpegts_options_group = new Fl_Flex(110, 110, 260, 127);
              mpegts_options_group->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              mpegts_options_group->hide();
              {
                input_listen_port =
                    new Fl_Input(110, 110, 260, 25, "Listen Port");
                input_listen_port->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              }  // Fl_Input* input_listen_port
              mpegts_options_group->gap(12);
              mpegts_options_group->fixed(mpegts_options_group->child(0), 25);
              mpegts_options_group->end();
            }  // Fl_Flex* mpegts_options_group
            {
              sdp_options_group = new Fl_Flex(100, 100, 260, 127);
              sdp_options_group->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              sdp_options_group->hide();
              {
                btn_open_sdp =
                    new Fl_Button(100, 100, 260, 25, "Open SDP File");
              }  // Fl_Button* btn_open_sdp
              sdp_options_group->gap(12);
              sdp_options_group->fixed(sdp_options_group->child(0), 25);
              sdp_options_group->end();
            }  // Fl_Flex* sdp_options_group
            {
              ndi_options_group = new Fl_Flex(100, 252, 260, 128);
              ndi_options_group->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              ndi_options_group->hide();
              {
                choice_ndi_input =
                    new Fl_Choice(100, 252, 260, 25, "NDI Input");
                choice_ndi_input->down_box(FL_BORDER_BOX);
                choice_ndi_input->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              }  // Fl_Choice* choice_ndi_input
              {
                btn_refresh_ndi_devices =
                    new Fl_Button(100, 289, 260, 25, "Refresh Devices");
              }  // Fl_Button* btn_refresh_ndi_devices
              ndi_options_group->gap(12);
              ndi_options_group->fixed(ndi_options_group->child(0), 25);
              ndi_options_group->fixed(ndi_options_group->child(1), 25);
              ndi_options_group->end();
            }  // Fl_Flex* ndi_options_group
            {
              // JPEG XS capture (MC4): a picker of LAN cameras plus a refresh
              // button, laid out like the NDI group.
              capture_options_group = new Fl_Flex(100, 252, 260, 128);
              capture_options_group->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              capture_options_group->hide();
              {
                choice_capture_input =
                    new Fl_Choice(100, 252, 260, 25, "Camera");
                choice_capture_input->down_box(FL_BORDER_BOX);
                choice_capture_input->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              }  // Fl_Choice* choice_capture_input
              {
                btn_refresh_capture =
                    new Fl_Button(100, 289, 260, 25, "Find Cameras");
              }  // Fl_Button* btn_refresh_capture
              {
                capture_state_output = new Fl_Output(100, 326, 260, 25);
                capture_state_output->align(Fl_Align(FL_ALIGN_TOP_LEFT));
                capture_state_output->value("Not searched yet - press Find.");
              }  // Fl_Output* capture_state_output
              capture_options_group->gap(12);
              capture_options_group->fixed(capture_options_group->child(0), 25);
              capture_options_group->fixed(capture_options_group->child(1), 25);
              capture_options_group->end();
            }  // Fl_Flex* capture_options_group
            {
              btn_preview_input =
                  new Fl_Button(31, 101, 421, 25, "Preview Input");
            }  // Fl_Button* btn_preview_input
            flx_input->margin(5, 25, 5, 5);
            flx_input->gap(25);
            flx_input->fixed(flx_input->child(0), 25);
            flx_input->fixed(ndi_options_group, 62);
            flx_input->fixed(capture_options_group, 62);
            flx_input->fixed(btn_preview_input, 25);
            flx_input->end();
          }  // Fl_Flex* flx_input
          {
            Fl_Flex* o = new Fl_Flex(25, 215, 433, 165, "Encode");
            o->box(FL_BORDER_BOX);
            {
              // "Send as": this is the codec the ENCODER puts on the wire. What
              // the host then does with it per destination is the portal's
              // transcode setting, shown in the log at sign-in.
              choice_codec =
                  new Fl_Choice(31, 161, 421, 25, "Send as (over RIST)");
              choice_codec->down_box(FL_BORDER_BOX);
              choice_codec->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              choice_codec->menu(menu_choice_codec);
            }  // Fl_Choice* choice_codec
            {
              choice_encoder = new Fl_Choice(31, 211, 421, 25, "Encoder");
              choice_encoder->down_box(FL_BORDER_BOX);
              choice_encoder->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              choice_encoder->menu(menu_choice_encoder);
            }  // Fl_Choice* choice_encoder
            {
              input_encode_bitrate = new Fl_Input(31, 261, 421, 25, "Bitrate");
              input_encode_bitrate->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            }  // Fl_Input* input_encode_bitrate
            {
              choice_bitrate_source =
                  new Fl_Choice(31, 311, 421, 25, "Scaling Source");
              choice_bitrate_source->down_box(FL_BORDER_BOX);
              choice_bitrate_source->align(Fl_Align(FL_ALIGN_TOP_LEFT));
              choice_bitrate_source->menu(menu_choice_bitrate_source);
            }  // Fl_Choice* choice_bitrate_source
            o->margin(5, 25, 5, 5);
            o->gap(25);
            o->fixed(o->child(0), 25);
            o->fixed(o->child(1), 25);
            o->fixed(o->child(2), 25);
            o->fixed(o->child(3), 25);
            o->end();
          }  // Fl_Flex* o
          o->gap(25);
          o->end();
        }  // Fl_Flex* o
        {
          Fl_Flex* o = new Fl_Flex(470, 25, 433, 309, "Output");
          o->box(FL_BORDER_BOX);
          {
            input_rist_address = new Fl_Input(476, 51, 421, 25, "RIST Address");
            input_rist_address->align(Fl_Align(FL_ALIGN_TOP_LEFT));
          }  // Fl_Input* input_rist_address
          {
            input_mpegts_alignment =
                new Fl_Input(476, 51, 421, 25, "MPEG-TS Alignment");
            input_mpegts_alignment->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            input_mpegts_alignment->tooltip(
                "TS packets per RIST datagram (1-7) = bytes/188. Lower it "
                "(e.g. 6) for low-MTU cellular links so RIST packets aren't "
                "IP-fragmented. Default 7. Applied at next Start.");
          }  // Fl_Input* input_mpegts_alignment
          {
            Fl_Flex* o = new Fl_Flex(475, 50, 423, 25);
            o->type(1);
            {
              btn_start_encode =
                  new Fl_Button(470, 50, 211, 25, "Start Encode");
            }  // Fl_Button* btn_start_encode
            {
              btn_stop_encode = new Fl_Button(693, 50, 210, 25, "Stop Encode");
              btn_stop_encode->deactivate();
            }  // Fl_Button* btn_stop_encode
            {
              btn_save_settings =
                  new Fl_Button(905, 50, 110, 25, "Save Settings");
            }  // Fl_Button* btn_save_settings
            {
              btn_exit = new Fl_Button(1020, 50, 80, 25, "Exit");
            }  // Fl_Button* btn_exit
            o->gap(12);
            o->end();
          }  // Fl_Flex* o
          {
            encode_state_output =
                new Fl_Output(476, 76, 421, 25, "Encode State");
            encode_state_output->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            encode_state_output->value("Idle");
          }  // Fl_Output* encode_state_output
          o->margin(5, 25, 5, 5);
          o->gap(25);
          o->fixed(o->child(0), 25);  // RIST Address
          o->fixed(o->child(1), 25);  // MPEG-TS Alignment
          o->fixed(o->child(2), 25);  // Start/Stop/Save/Exit button row
          o->fixed(o->child(3), 25);  // Encode State
          o->end();
        }  // Fl_Flex* o
        {
          Fl_Flex* o = new Fl_Flex(915, 25, 433, 309, "Stats");
          o->box(FL_BORDER_BOX);
          {
            grid_stats = new Fl_Grid(921, 26, 421, 256);
            grid_stats->layout(8, 3);
            static const int rowheights[] = {25, 25, 25, 25, 25, 25, 25, 0};
            grid_stats->row_height(rowheights, 8);
            static const int colwidths[] = {50, 50, 0};
            grid_stats->col_width(colwidths, 3);
            {
              bandwidth_output = new Fl_Output(1060, 57, 144, 32, "Bandwidth");
              bandwidth_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* bandwidth_output
            {
              link_quality_output =
                  new Fl_Output(1060, 89, 144, 32, "Link Quality");
              link_quality_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* link_quality_output
            {
              retransmitted_packets_output =
                  new Fl_Output(1060, 121, 144, 32, "Retransmitted Packets");
              retransmitted_packets_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* retransmitted_packets_output
            {
              rtt_output = new Fl_Output(1060, 153, 144, 32, "RTT");
              rtt_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* rtt_output
            {
              total_packets_output =
                  new Fl_Output(1060, 185, 144, 32, "Packets");
              total_packets_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* total_packets_output
            {
              encode_bitrate_output =
                  new Fl_Output(1060, 249, 144, 32, "Encode Bitrate");
              encode_bitrate_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* encode_bitrate_output
            {
              cumulative_bandwidth_output =
                  new Fl_Output(1204, 57, 144, 32, "Bandwidth");
              cumulative_bandwidth_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* cumulative_bandwidth_output
            {
              cumulative_retransmitted_packets_output =
                  new Fl_Output(1204, 121, 144, 32, "Retransmitted Packets");
              cumulative_retransmitted_packets_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* cumulative_retransmitted_packets_output
            {
              cumulative_total_packets_output =
                  new Fl_Output(1204, 185, 144, 32, "Packets");
              cumulative_total_packets_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* cumulative_total_packets_output
            {
              cumulative_encode_bitrate_output =
                  new Fl_Output(1204, 249, 144, 32, "Encode Bitrate");
              cumulative_encode_bitrate_output->labeltype(FL_NO_LABEL);
            }  // Fl_Output* cumulative_encode_bitrate_output
            {
              new Fl_Box(1204, 25, 144, 32, "Total");
            }  // Fl_Box* o
            {
              new Fl_Box(1060, 25, 144, 32, "Current");
            }  // Fl_Box* o
            {
              Fl_Box* o = new Fl_Box(915, 57, 145, 32, "Bandwidth");
              o->align(Fl_Align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE));
            }  // Fl_Box* o
            {
              Fl_Box* o = new Fl_Box(915, 89, 145, 32, "Link Quality");
              o->align(Fl_Align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE));
            }  // Fl_Box* o
            {
              Fl_Box* o = new Fl_Box(915, 121, 145, 32, "Retransmitted");
              o->align(Fl_Align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE));
            }  // Fl_Box* o
            {
              Fl_Box* o = new Fl_Box(915, 153, 145, 32, "RTT");
              o->align(Fl_Align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE));
            }  // Fl_Box* o
            {
              Fl_Box* o = new Fl_Box(915, 185, 145, 32, "Packets");
              o->align(Fl_Align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE));
            }  // Fl_Box* o
            {
              Fl_Box* o = new Fl_Box(915, 249, 145, 32, "Encode Bitrate");
              o->align(Fl_Align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE));
            }  // Fl_Box* o
            Fl_Grid::Cell* cell = NULL;
            cell = grid_stats->widget(grid_stats->child(0), 1, 1, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(1), 2, 1, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(2), 3, 1, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(3), 4, 1, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(4), 5, 1, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(5), 7, 1, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(6), 1, 2, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(7), 3, 2, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(8), 5, 2, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(9), 7, 2, 1, 1, 48);
            if (cell)
              cell->minimum_size(50, 25);
            cell = grid_stats->widget(grid_stats->child(10), 0, 2, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(11), 0, 1, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(16), 5, 0, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            cell = grid_stats->widget(grid_stats->child(17), 7, 0, 1, 1, 48);
            if (cell)
              cell->minimum_size(20, 20);
            grid_stats->end();
          }  // Fl_Grid* grid_stats
          {
            flx_wan_stats = new Fl_Flex(915, 286, 433, 25);
            flx_wan_stats->type(1);
            {
              wan_quality_output =
                  new Fl_Output(1060, 286, 144, 25, "WAN Quality");
              wan_quality_output->align(Fl_Align(FL_ALIGN_LEFT));
            }  // Fl_Output* wan_quality_output
            {
              wan_rtt_output = new Fl_Output(1204, 286, 144, 25, "WAN RTT");
              wan_rtt_output->align(Fl_Align(FL_ALIGN_LEFT));
            }  // Fl_Output* wan_rtt_output
            flx_wan_stats->gap(80);
            flx_wan_stats->end();
          }  // Fl_Flex* flx_wan_stats
          o->margin(5, 0, 5, 0);
          o->gap(5);
          o->fixed(o->child(0), 256);
          o->fixed(o->child(1), 25);
          o->end();
        }  // Fl_Flex* o
        flx_top->margin(0, 0, 0, 12);
        flx_top->gap(12);
        flx_top->end();
      }  // Fl_Flex* flx_top
      {
        flx_receiver = new Fl_Flex(25, 442, 1323, 150, "Receiver / Restream");
        flx_receiver->box(FL_BORDER_BOX);
        {
          Fl_Flex* row = new Fl_Flex(25, 464, 1323, 25);
          row->type(1);
          {
            check_receiver_enabled =
                new Fl_Check_Button(0, 0, 110, 25, "Enable receiver");
          }  // Fl_Check_Button* check_receiver_enabled
          {
            input_control_address =
                new Fl_Input(0, 0, 180, 25, "Receiver host:port");
            input_control_address->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            input_control_address->value("127.0.0.1:8080");
          }  // Fl_Input* input_control_address
          {
            input_control_token = new Fl_Input(0, 0, 160, 25, "Token");
            input_control_token->align(Fl_Align(FL_ALIGN_TOP_LEFT));
          }  // Fl_Input* input_control_token
          row->gap(10);
          row->end();
        }  // Fl_Flex* row
        {
          input_destinations =
              new Fl_Multiline_Input(25,
                                     487,
                                     1323,
                                     90,
                                     "Destinations (one per line:  "
                                     "rtmp|rtmps|srt|rist  <url>  [key])");
          input_destinations->align(Fl_Align(FL_ALIGN_TOP_LEFT));
        }  // Fl_Multiline_Input* input_destinations
        flx_receiver->margin(8, 22, 8, 8);
        flx_receiver->gap(22);
        flx_receiver->fixed(flx_receiver->child(0), 25);
        flx_receiver->end();
      }  // Fl_Flex* flx_receiver
      {
        flx_bridge = new Fl_Flex(25, 442, 1323, 167, "Bridge (LAN)");
        flx_bridge->box(FL_BORDER_BOX);
        {
          Fl_Flex* row = new Fl_Flex(25, 464, 1323, 25);
          row->type(1);
          {
            input_bridge_address =
                new Fl_Input(0, 0, 200, 25, "Bridge address");
            input_bridge_address->align(Fl_Align(FL_ALIGN_TOP_LEFT));
          }  // Fl_Input* input_bridge_address
          {
            btn_bridge_find = new Fl_Button(0, 0, 90, 25, "Find");
          }  // Fl_Button* btn_bridge_find
          {
            btn_bridge_claim = new Fl_Button(0, 0, 90, 25, "Claim");
          }  // Fl_Button* btn_bridge_claim
          {
            btn_bridge_apply = new Fl_Button(0, 0, 90, 25, "Apply");
          }  // Fl_Button* btn_bridge_apply
          {
            // DT-28: measure the WAN legs and set the shaper and bond weights.
            // Disabled while a stream is up -- the bridge refuses a mid-stream
            // probe itself, and this is the operator-facing half of that rule.
            btn_bridge_calibrate =
                new Fl_Button(0, 0, 110, 25, "Calibrate links");
          }  // Fl_Button* btn_bridge_calibrate
          row->gap(10);
          row->end();
        }  // Fl_Flex* row
        {
          Fl_Flex* row = new Fl_Flex(25, 489, 1323, 25);
          row->type(1);
          {
            input_bridge_listen =
                new Fl_Input(0, 0, 230, 25, "Bridge listens on");
            input_bridge_listen->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            input_bridge_listen->value("rist://0.0.0.0:5000");
          }  // Fl_Input* input_bridge_listen
          {
            input_bridge_forward =
                new Fl_Input(0, 0, 230, 25, "Bridge forwards to");
            input_bridge_forward->align(Fl_Align(FL_ALIGN_TOP_LEFT));
          }  // Fl_Input* input_bridge_forward
          {
            input_bridge_interface = new Fl_Input(0, 0, 90, 25, "Interface");
            input_bridge_interface->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            input_bridge_interface->value("wan");
          }  // Fl_Input* input_bridge_interface
          {
            bridge_token_output = new Fl_Output(0, 0, 140, 25, "Pair token");
            bridge_token_output->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            bridge_token_output->value("not claimed");
          }  // Fl_Output* bridge_token_output
          row->gap(10);
          row->end();
        }  // Fl_Flex* row
        {
          // The state line gets its own full-width row. It is the longest text
          // in the group AND it carries the bridge's address, so letting a flex
          // row distribute it would clip the one thing the operator must read.
          bridge_state_output =
              new Fl_Output(25, 514, 1323, 25, "Bridge state");
          bridge_state_output->align(Fl_Align(FL_ALIGN_TOP_LEFT));
          bridge_state_output->value("Not found yet - press Find.");
        }  // Fl_Output* bridge_state_output
        {
          // The calibration report gets its own full-width row: it is several
          // lines of per-leg numbers, and a flex row would clip exactly the
          // figures the operator pressed the button to see.
          bridge_calibrate_output =
              new Fl_Output(25, 539, 1323, 25, "Link calibration");
          bridge_calibrate_output->align(Fl_Align(FL_ALIGN_TOP_LEFT));
          bridge_calibrate_output->value("not run");
        }  // Fl_Output* bridge_calibrate_output
        flx_bridge->margin(8, 22, 8, 8);
        flx_bridge->gap(12);
        flx_bridge->end();
      }  // Fl_Flex* flx_bridge
      {
        flx_hosted = new Fl_Flex(25, 442, 1323, 92, "Hosted (portal)");
        flx_hosted->box(FL_BORDER_BOX);
        {
          Fl_Flex* row = new Fl_Flex(25, 464, 1323, 25);
          row->type(1);
          {
            input_backplane_url = new Fl_Input(0, 0, 420, 25, "Backplane URL");
            input_backplane_url->align(Fl_Align(FL_ALIGN_TOP_LEFT));
          }  // Fl_Input* input_backplane_url
          {
            btn_hosted_signin = new Fl_Button(0, 0, 90, 25, "Sign in");
          }  // Fl_Button* btn_hosted_signin
          {
            btn_hosted_signout = new Fl_Button(0, 0, 90, 25, "Sign out");
          }  // Fl_Button* btn_hosted_signout
          {
            btn_hosted_allocate = new Fl_Button(0, 0, 100, 25, "Allocate");
          }  // Fl_Button* btn_hosted_allocate
          {
            hosted_token_output = new Fl_Output(0, 0, 140, 25, "Device token");
            hosted_token_output->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            hosted_token_output->value("not signed in");
          }  // Fl_Output* hosted_token_output
          row->gap(10);
          row->end();
        }  // Fl_Flex* row
        {
          // Its own full-width row, like the bridge state line -- the sign-in
          // instruction has to be readable in one piece.
          hosted_state_output =
              new Fl_Output(25, 489, 1323, 25, "Hosted state");
          hosted_state_output->align(Fl_Align(FL_ALIGN_TOP_LEFT));
          hosted_state_output->value(
              "Not signed in - the portal half stays inactive until you sign "
              "in.");
        }  // Fl_Output* hosted_state_output
        flx_hosted->margin(8, 22, 8, 8);
        flx_hosted->gap(12);
        flx_hosted->end();
      }  // Fl_Flex* flx_hosted
      {
        flx_bottom = new Fl_Flex(25, 442, 1323, 200);
        flx_bottom->type(1);
        {
          transport_log_display = new Fl_Text_Display(25, 442, 662, 200);
        }  // Fl_Text_Display* transport_log_display
        {
          encode_log_display = new Fl_Text_Display(687, 442, 661, 200);
        }  // Fl_Text_Display* encode_log_display
        flx_bottom->end();
      }  // Fl_Flex* flx_bottom
      pack->margin(25, 25, 25, 25);
      pack->fixed(flx_receiver, 150);
      pack->fixed(flx_bridge, 167);
      pack->fixed(flx_hosted, 92);
      pack->fixed(flx_bottom, 200);
      pack->end();
    }  // Fl_Flex* pack
    main_window->resizable(pack);
    main_window->end();
  }  // Fl_Double_Window* main_window
}

void user_interface::show(int argc, char** argv) const
{
  main_window->show(argc, argv);
}

void user_interface::layout()
{
  pack->layout();
  flx_top->layout();
  flx_bottom->layout();
}

void user_interface::transport_log_append(const std::string& msg) const
{
  // M1.9: no stream key / token / PSK material ever reaches the log panes.
  const std::string clean = secrets::redact(msg);
  Fl::lock();
  transport_log_display->insert(clean.c_str());
  Fl::unlock();
  Fl::awake();
}

void user_interface::set_encode_state(const std::string& text, bool is_failed)
{
  lock();
  encode_state_output->value(text.c_str());
  encode_state_output->textcolor(is_failed ? FL_RED : FL_BLACK);
  encode_state_output->redraw();
  // A failed pipeline has already ended the send loop, so present the buttons
  // as stopped rather than leaving a dead "running" pair on screen.
  if (is_failed) {
    btn_start_encode->activate();
    btn_stop_encode->deactivate();
  }
  unlock();
  Fl::awake();
}

void user_interface::encode_log_append(const std::string& msg) const
{
  const std::string clean = secrets::redact(msg);
  Fl::lock();
  encode_log_display->insert(clean.c_str());
  Fl::unlock();
  Fl::awake();
}

void user_interface::init_ui()
{
  Fl::visual(FL_DOUBLE | FL_INDEX);
  Fl::lock();
}

void user_interface::lock()
{
  Fl::lock();
}

void user_interface::unlock()
{
  Fl::unlock();
  Fl::awake();
}

int user_interface::run_ui()
{
  return Fl::run();
}

void user_interface::choose_input_protocol(input_config* input_config,
                                           FuncPtr refresh_ndi_funcptr,
                                           FuncPtr refresh_capture_funcptr)
{
  switch (
      reinterpret_cast<uintptr_t>(choice_input_protocol->mvalue()->user_data()))
  {
    case 0: {
      input_config->selected_input_mode = input_mode::testsrc;
      Fl::lock();
      mpegts_options_group->hide();
      sdp_options_group->hide();
      ndi_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
      break;
    }

    case 1: {
      input_config->selected_input_mode = input_mode::mpegts;
      Fl::lock();
      mpegts_options_group->show();
      ndi_options_group->hide();
      sdp_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
      break;
    }

    case 2: {
      input_config->selected_input_mode = input_mode::sdp;
      Fl::lock();
      sdp_options_group->show();
      ndi_options_group->hide();
      mpegts_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
      break;
    }

    case 3: {
      input_config->selected_input_mode = input_mode::ndi;
      Fl::lock();
      ndi_options_group->show();
      sdp_options_group->hide();
      mpegts_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
      refresh_ndi_funcptr();
      break;
    }

    case 4: {
      input_config->selected_input_mode = input_mode::raw_local;
      Fl::lock();
      // Shares the MPEG-TS option group: that field is just a listen port,
      // which is exactly what the OBS raw ingest needs.
      if (input_listen_port->value()[0] == '\0') {
        input_listen_port->value("9300");
      }
      mpegts_options_group->show();
      sdp_options_group->hide();
      ndi_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
      break;
    }

    case 5: {
      input_config->selected_input_mode = input_mode::jpegxs_capture;
      Fl::lock();
      // Shares the MPEG-TS option group's listen-port field: for a unicast
      // camera link that field is the STREAM PORT this side receives on, and
      // the camera node's default is 5000.
      if (input_listen_port->value()[0] == '\0') {
        input_listen_port->value("5000");
      }
      capture_options_group->show();
      mpegts_options_group->show();
      sdp_options_group->hide();
      ndi_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
      // Browsing blocks; run it on a tracked background thread.
      refresh_capture_funcptr();
      break;
    }

    default: {
      input_config->selected_input_mode = input_mode::none;
      Fl::lock();
      ndi_options_group->hide();
      capture_options_group->hide();
      sdp_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
    }
  }
}

void user_interface::add_ndi_choices(
    const std::vector<std::string>& choice_names)
{
  Fl::lock();
  ndi_choice_storage.reserve(ndi_choice_storage.size() + choice_names.size());
  for (const auto& name : choice_names) {
    ndi_choice_storage.push_back(name);
    char* user_data = ndi_choice_storage.back().data();
    choice_ndi_input->add(user_data, 0, nullptr, user_data, 0);
  }

  Fl::unlock();
  Fl::awake();
}

void user_interface::clear_ndi_choices()
{
  Fl::lock();
  choice_ndi_input->clear();
  ndi_choice_storage.clear();
  Fl::unlock();
  Fl::awake();
}

void user_interface::choose_ndi_input(input_config* input_config)
{
  auto input_name = static_cast<char*>(choice_ndi_input->mvalue()->user_data());
  input_config->selected_input = input_name;
}

// ---- Capture (MC4) picker --------------------------------------------------
// The browse runs on a tracked background thread (it blocks by design), so the
// worker populates the picker through set_capture_sources, which takes the FLTK
// lock. Selecting an item is a normal FLTK callback on the UI thread and writes
// the model directly, like choose_ndi_input.

void user_interface::choose_capture_input(input_config* input_config)
{
  if (choice_capture_input->mvalue() == nullptr) {
    return;
  }
  // The item's user_data is the camera's ADDRESS (stable storage); the label is
  // the name the operator sees. The reader needs the address; the name is kept
  // only for the log line.
  const auto* address =
      static_cast<const char*>(choice_capture_input->mvalue()->user_data());
  if (address != nullptr) {
    input_config->capture_address = address;
  }
  const char* label = choice_capture_input->mvalue()->label();
  if (label != nullptr) {
    input_config->capture_name = label;
  }
}

void user_interface::refresh_capture(FuncPtr refresh_capture_funcptr)
{
  if (refresh_capture_funcptr != nullptr) {
    refresh_capture_funcptr();
  }
}

void user_interface::set_capture_sources(
    const std::vector<std::pair<std::string, std::string>>& sources,
    const std::string& message,
    bool is_error)
{
  lock();
  // Reserve BEFORE pushing so the char* handed to the menu items (the label
  // text and the user_data address) stay valid as the vectors grow.
  capture_label_storage.clear();
  capture_address_storage.clear();
  capture_label_storage.reserve(sources.size());
  capture_address_storage.reserve(sources.size());
  choice_capture_input->clear();
  for (const auto& [label, address] : sources) {
    capture_label_storage.push_back(label);
    capture_address_storage.push_back(address);
  }
  for (std::size_t i = 0; i < capture_label_storage.size(); ++i) {
    choice_capture_input->add(capture_label_storage[i].c_str(),
                              0,
                              nullptr,
                              capture_address_storage[i].data(),
                              0);
  }
  if (choice_capture_input->size() > 0) {
    choice_capture_input->value(0);
  }
  capture_state_output->value(message.c_str());
  capture_state_output->textcolor(is_error ? FL_RED : FL_BLACK);
  capture_state_output->redraw();
  unlock();
  Fl::awake();
}

void user_interface::input_listen_port_cb(input_config* input_config)
{
  const char* raw = input_listen_port->value();
  if (raw == nullptr) {
    return;
  }
  try {
    int port = std::stoi(raw);
    if (port < 1 || port > 65535) {
      return;
    }
  } catch (...) {
    return;
  }
  input_config->selected_input = raw;
}

void user_interface::input_rist_address_cb(output_config* output_config,
                                           FuncPtr input_rist_address_funcptr)
{
  output_config->address = input_rist_address->value();
  auto [h, p] = parse_address(output_config->address);
  if (h.empty() || p < 1 || p > 65535) {
    return;
  }
  output_config->host = h;
  output_config->port = p;
  if (input_rist_address_funcptr != nullptr) {
    input_rist_address_funcptr();
  }
}

void user_interface::select_codec(encode_config* encode_config)
{
  auto user_data =
      reinterpret_cast<uintptr_t>(choice_codec->mvalue()->user_data());
  encode_config->selected_codec = static_cast<codec>(user_data);
}

void user_interface::select_encoder(encode_config* encode_config)
{
  auto user_data =
      reinterpret_cast<uintptr_t>(choice_encoder->mvalue()->user_data());

  encode_config->selected_encoder = static_cast<encoder>(user_data);
}

void user_interface::select_bitrate_source(
    encode_config* encode_config, FuncPtr scaling_source_changed_funcptr)
{
  auto user_data =
      reinterpret_cast<uintptr_t>(choice_bitrate_source->mvalue()->user_data());
  encode_config->scaling_source.store(static_cast<bitrate_source>(user_data),
                                      std::memory_order_relaxed);
  if (scaling_source_changed_funcptr != nullptr) {
    scaling_source_changed_funcptr();
  }
}

void user_interface::encode_bitrate_cb(encode_config* encode_config)
{
  const char* raw = input_encode_bitrate->value();
  if (raw == nullptr) {
    return;
  }
  try {
    encode_config->bitrate.store(std::stoi(raw), std::memory_order_relaxed);
  } catch (...) {
  }
}

void user_interface::mpegts_alignment_cb(encode_config* encode_config)
{
  const char* raw = input_mpegts_alignment->value();
  if (raw == nullptr) {
    return;
  }
  try {
    encode_config->mpegts_alignment.store(std::clamp(std::stoi(raw), 1, 7),
                                          std::memory_order_relaxed);
  } catch (...) {
  }
}

void user_interface::receiver_enabled_cb(receiver_control_config* rc)
{
  rc->enabled = check_receiver_enabled->value() != 0;
}

void user_interface::receiver_address_cb(receiver_control_config* rc)
{
  const char* raw = input_control_address->value();
  if (raw == nullptr) {
    return;
  }
  std::string host;
  int port = 8080;  // control-plane default (NOT the RIST media default 5000)
  parse_control_address(raw, host, port);
  rc->control_host = host;
  rc->control_port = port;
}

void user_interface::receiver_token_cb(receiver_control_config* rc)
{
  const char* raw = input_control_token->value();
  rc->token = (raw != nullptr) ? raw : "";
}

void user_interface::receiver_destinations_cb(receiver_control_config* rc)
{
  rc->destinations = parse_destinations(input_destinations->value());
}

// ---- Bridge (LAN) control callbacks ---------------------------------------

void user_interface::bridge_address_cb(bridge_control_config* bridge_config)
{
  bridge_config->address = input_bridge_address->value();
  // A new address means whatever bridge we had found is no longer the one being
  // pointed at, so drop the identity rather than acting on a stale uid.
  bridge_config->bridge_uid.clear();
}

void user_interface::bridge_listen_cb(bridge_control_config* bridge_config)
{
  bridge_config->listen_url = input_bridge_listen->value();
}

void user_interface::bridge_forward_cb(bridge_control_config* bridge_config)
{
  bridge_config->forward_to = input_bridge_forward->value();
}

void user_interface::bridge_interface_cb(bridge_control_config* bridge_config)
{
  bridge_config->interface_name = input_bridge_interface->value();
}

// The three actions themselves live in main.cpp: they need discovery and the
// ubus transport, and every one of them runs on a tracked background thread so
// the browse cannot block the UI.

void user_interface::bridge_find(FuncPtr find_funcptr)
{
  find_funcptr();
}

void user_interface::bridge_claim(FuncPtr claim_funcptr)
{
  claim_funcptr();
}

void user_interface::bridge_apply(FuncPtr apply_funcptr)
{
  apply_funcptr();
}

void user_interface::bridge_calibrate(FuncPtr calibrate_funcptr)
{
  calibrate_funcptr();
}

void user_interface::set_bridge_calibration(const std::string& text,
                                            bool is_error)
{
  lock();
  if (bridge_calibrate_output != nullptr) {
    bridge_calibrate_output->value(text.c_str());
    bridge_calibrate_output->textcolor(is_error ? FL_RED : FL_BLACK);
    bridge_calibrate_output->redraw();
  }
  unlock();
  Fl::awake();
}

void user_interface::set_bridge_calibrate_enabled(bool on)
{
  lock();
  if (btn_bridge_calibrate != nullptr) {
    // DT-28: the button exists to be pressed BEFORE a stream. The bridge
    // refuses a mid-stream calibration itself, so this only makes the refusal
    // arrive before the press rather than after it.
    if (on) {
      btn_bridge_calibrate->activate();
    } else {
      btn_bridge_calibrate->deactivate();
    }
    btn_bridge_calibrate->redraw();
  }
  unlock();
  Fl::awake();
}

// ---- Hosted (portal) control callbacks -------------------------------------

void user_interface::hosted_url_cb(hosted_config* hosted_config)
{
  hosted_config->backplane_url = input_backplane_url->value();
  // Deliberately does NOT clear the device token. This fires on every
  // keystroke, so clearing here would sign the operator out as they type the
  // URL; a token from a different backplane is refused by that backplane
  // instead, which the state line reports.
}

// The sign-in itself lives in main.cpp: it needs the device-authorization
// client and runs on a tracked background thread, because polling blocks.

void user_interface::hosted_sign_in(FuncPtr signin_funcptr)
{
  signin_funcptr();
}

void user_interface::hosted_sign_out(FuncPtr signout_funcptr)
{
  signout_funcptr();
}

// One Allocate action (DT-20.1). The work lives in main.cpp: it allocates,
// applies the bridge the portal chose over the LAN, and reports it -- all
// blocking, so it runs on a tracked background thread.
void user_interface::hosted_allocate(FuncPtr allocate_funcptr)
{
  allocate_funcptr();
}

void user_interface::start(void (*start_funcptr)())
{
  lock();
  btn_start_encode->deactivate();
  btn_stop_encode->activate();
  unlock();
  start_funcptr();
}

void user_interface::stop(void (*stop_funcptr)())
{
  lock();
  btn_start_encode->activate();
  btn_stop_encode->deactivate();
  unlock();
  stop_funcptr();
}

void user_interface::save_settings(FuncPtr save_settings_funcptr)
{
  if (save_settings_funcptr != nullptr) {
    save_settings_funcptr();
  }
}

void user_interface::apply_settings(const input_config& input_c,
                                    const encode_config& encode_c,
                                    const output_config& output_c,
                                    const receiver_control_config& receiver_c,
                                    const bridge_control_config& bridge_c,
                                    const hosted_config& hosted_c)
{
  // ---- Input ----
  select_choice_by_userdata(choice_input_protocol,
                            static_cast<long>(input_c.selected_input_mode));
  // Mirror choose_input_protocol()'s option-group visibility for the restored
  // mode so the UI is consistent without synthesising a user click.
  mpegts_options_group->hide();
  sdp_options_group->hide();
  ndi_options_group->hide();
  capture_options_group->hide();
  switch (input_c.selected_input_mode) {
    case input_mode::mpegts:
      input_listen_port->value(input_c.selected_input.c_str());
      mpegts_options_group->show();
      break;
    case input_mode::raw_local:
      input_listen_port->value(input_c.selected_input.empty()
                                   ? "9300"
                                   : input_c.selected_input.c_str());
      mpegts_options_group->show();
      break;
    case input_mode::sdp:
      sdp_options_group->show();
      break;
    case input_mode::ndi:
      // The saved device name may not be present yet; the NDI monitor
      // repopulates choice_ndi_input. The model already holds selected_input.
      ndi_options_group->show();
      break;
    case input_mode::jpegxs_capture:
      input_listen_port->value(input_c.selected_input.empty()
                                   ? "5000"
                                   : input_c.selected_input.c_str());
      mpegts_options_group->show();
      capture_options_group->show();
      // Show the SAVED camera as the only item until a browse repopulates the
      // list. The address is what the reader binds toward; the label is the
      // saved name (or the address when no name was kept).
      if (!input_c.capture_address.empty()) {
        capture_label_storage.clear();
        capture_address_storage.clear();
        capture_label_storage.push_back(input_c.capture_name.empty()
                                            ? input_c.capture_address
                                            : input_c.capture_name);
        capture_address_storage.push_back(input_c.capture_address);
        choice_capture_input->clear();
        choice_capture_input->add(capture_label_storage.back().c_str(),
                                  0,
                                  nullptr,
                                  capture_address_storage.back().data(),
                                  0);
        choice_capture_input->value(0);
        capture_state_output->value("Saved selection - press Find to refresh.");
      }
      break;
    case input_mode::testsrc:
    case input_mode::none:
      break;
  }

  // ---- Encode ----
  select_choice_by_userdata(choice_codec,
                            static_cast<long>(encode_c.selected_codec));
  select_choice_by_userdata(choice_encoder,
                            static_cast<long>(encode_c.selected_encoder));
  input_encode_bitrate->value(
      std::to_string(encode_c.bitrate.load(std::memory_order_relaxed)).c_str());
  input_mpegts_alignment->value(
      std::to_string(encode_c.mpegts_alignment.load(std::memory_order_relaxed))
          .c_str());
  select_choice_by_userdata(choice_bitrate_source,
                            static_cast<long>(encode_c.scaling_source.load(
                                std::memory_order_relaxed)));

  // ---- Output ----
  input_rist_address->value(output_c.address.c_str());

  // ---- Receiver / Restream ----
  check_receiver_enabled->value(receiver_c.enabled ? 1 : 0);
  input_control_address->value(
      (receiver_c.control_host + ":" + std::to_string(receiver_c.control_port))
          .c_str());
  input_control_token->value(receiver_c.token.c_str());
  input_destinations->value(
      format_destinations(receiver_c.destinations).c_str());

  // ---- Bridge (LAN) ----
  // Written directly rather than through the setters: this runs once on the
  // main thread before show(), and the setters take the FLTK lock.
  input_bridge_address->value(bridge_c.address.c_str());
  input_bridge_listen->value(bridge_c.listen_url.c_str());
  input_bridge_forward->value(bridge_c.forward_to.c_str());
  input_bridge_interface->value(bridge_c.interface_name.c_str());
  bridge_token_output->value(bridge_c.token.empty() ? "not claimed" : "set");
  bridge_state_output->value(bridge_c.bridge_uid.empty()
                                 ? "Not found yet - press Find."
                                 : bridge_c.bridge_uid.c_str());

  // ---- Hosted (portal) ----
  input_backplane_url->value(hosted_c.backplane_url.c_str());
  hosted_token_output->value(hosted_c.device_token.empty() ? "not signed in"
                                                           : "set");
  hosted_state_output->value(
      hosted_c.device_token.empty()
          ? "Not signed in - the portal half stays inactive until you sign in."
          : "Signed in.");

  layout();
}

// ---- Bridge (LAN) state writable from a background thread ------------------
// Every worker-driven UI write goes through one of these: the bridge actions
// run on a tracked thread (bridge::discover() blocks by design), so neither the
// widgets nor the config may be touched without the FLTK lock. Holding the lock
// is what makes updating the model here safe -- FLTK dispatches widget
// callbacks under the same lock.

void user_interface::set_bridge_discovered(const std::string& uid,
                                           const std::string& address,
                                           const std::string& text,
                                           bool is_error)
{
  lock();
  if (bridge_config_ptr != nullptr) {
    if (!uid.empty()) {
      bridge_config_ptr->bridge_uid = uid;
    }
    if (!address.empty()) {
      bridge_config_ptr->address = address;
      input_bridge_address->value(address.c_str());
    }
  }
  bridge_state_output->value(text.c_str());
  bridge_state_output->textcolor(is_error ? FL_RED : FL_BLACK);
  bridge_state_output->redraw();
  unlock();
  Fl::awake();
}

void user_interface::set_bridge_token(const std::string& token)
{
  // M1.9/H2: the token is never rendered, logged or put in a URL. Register it
  // before anything else can see it, and show only that one is held.
  if (!token.empty()) {
    secrets::register_secret(token);
  }
  lock();
  if (bridge_config_ptr != nullptr) {
    bridge_config_ptr->token = token;
  }
  bridge_token_output->value(token.empty() ? "not claimed" : "set");
  bridge_token_output->redraw();
  unlock();
  Fl::awake();
}

void user_interface::set_bridge_message(const std::string& text, bool is_error)
{
  lock();
  bridge_state_output->value(text.c_str());
  bridge_state_output->textcolor(is_error ? FL_RED : FL_BLACK);
  bridge_state_output->redraw();
  unlock();
  Fl::awake();
}

void user_interface::set_hosted_state(const std::string& text, bool is_error)
{
  lock();
  hosted_state_output->value(text.c_str());
  hosted_state_output->textcolor(is_error ? FL_RED : FL_BLACK);
  hosted_state_output->redraw();
  unlock();
  Fl::awake();
}

// The encoder's send target, decided by the allocation (DT-20.1). Model and
// widget together under the lock, exactly like the bridge setters: a worker
// thread must never write the config directly. The RIST sender is built at
// Start on the run_loop thread, so moving the target here does not disturb a
// live pipeline.
void user_interface::set_encoder_target(const std::string& url)
{
  if (url.empty()) {
    return;
  }
  lock();
  if (output_config_ptr != nullptr) {
    output_config_ptr->address = url;
    // The sender splits host and port out of this at Start; keep them
    // consistent so a later save does not persist a stale pair.
    const std::string::size_type colon = url.rfind(':');
    if (colon != std::string::npos && url.compare(0, 7, "rist://") == 0) {
      output_config_ptr->host = url.substr(7, colon - 7);
      try {
        output_config_ptr->port = std::stoi(url.substr(colon + 1));
      } catch (const std::exception&) {
        // A non-numeric port is the sender's to reject, not this setter's.
      }
    }
    input_rist_address->value(url.c_str());
  }
  unlock();
  Fl::awake();
}

void user_interface::set_hosted_token(const std::string& token)
{
  // The device token authorises everything against the account, so it is never
  // rendered: register it before it can reach a log line, and show only that
  // one is held (H2).
  if (!token.empty()) {
    secrets::register_secret(token);
  }
  lock();
  if (hosted_config_ptr != nullptr) {
    hosted_config_ptr->device_token = token;
    if (token.empty()) {
      // Signing out invalidates the derived ids too: they belong to the account
      // the token was minted for.
      hosted_config_ptr->device_id = 0;
      hosted_config_ptr->bridge_id = 0;
    }
  }
  hosted_token_output->value(token.empty() ? "not signed in" : "set");
  hosted_token_output->redraw();
  unlock();
  Fl::awake();
}

void user_interface::set_hosted_ids(long device_id, long bridge_id)
{
  lock();
  if (hosted_config_ptr != nullptr) {
    hosted_config_ptr->device_id = device_id;
    hosted_config_ptr->bridge_id = bridge_id;
  }
  unlock();
  Fl::awake();
}

void user_interface::refresh_ndi_devices(FuncPtr refresh_ndi_funcptr)
{
  refresh_ndi_funcptr();
}

void user_interface::btn_preview_input_cb(FuncPtr preview_src_funcptr)
{
  preview_src_funcptr();
}

void user_interface::init_ui_callbacks(input_config* input_c,
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
                                       FuncPtr refresh_capture_funcptr)
{
  main_window->callback([](Fl_Widget* w, void*) { w->hide(); });

  transport_log_display->buffer(transport_log_buffer);
  encode_log_display->buffer(encode_log_buffer);

  FL_METHOD_CALLBACK_3(choice_input_protocol,
                       user_interface,
                       this,
                       choose_input_protocol,
                       input_config*,
                       input_c,
                       FuncPtr,
                       ndi_refresh_funcptr,
                       FuncPtr,
                       refresh_capture_funcptr);

  // The protocol dropdown has no value until the user actively selects an
  // item, leaving mvalue() == nullptr and selected_input_mode == none. In that
  // state Preview/Start silently no-op. Default to the first item (Test Source)
  // and sync the model so the buttons act on a real input mode out of the box.
  // The option groups already default to hidden, which matches testsrc.
  choice_input_protocol->value(select_test_input);
  input_c->selected_input_mode = input_mode::testsrc;

  FL_METHOD_CALLBACK_1(choice_ndi_input,
                       user_interface,
                       this,
                       choose_ndi_input,
                       input_config*,
                       input_c);

  FL_METHOD_CALLBACK_1(choice_capture_input,
                       user_interface,
                       this,
                       choose_capture_input,
                       input_config*,
                       input_c);

  FL_METHOD_CALLBACK_1(btn_refresh_capture,
                       user_interface,
                       this,
                       refresh_capture,
                       FuncPtr,
                       refresh_capture_funcptr);

  FL_METHOD_CALLBACK_1(input_listen_port,
                       user_interface,
                       this,
                       input_listen_port_cb,
                       input_config*,
                       input_c);

  FL_METHOD_CALLBACK_1(choice_codec,
                       user_interface,
                       this,
                       select_codec,
                       encode_config*,
                       encode_c);

  FL_METHOD_CALLBACK_1(choice_encoder,
                       user_interface,
                       this,
                       select_encoder,
                       encode_config*,
                       encode_c);

  FL_METHOD_CALLBACK_2(choice_bitrate_source,
                       user_interface,
                       this,
                       select_bitrate_source,
                       encode_config*,
                       encode_c,
                       FuncPtr,
                       scaling_source_changed_funcptr);

  input_encode_bitrate->value("4300");
  input_encode_bitrate->when(FL_WHEN_CHANGED);
  FL_METHOD_CALLBACK_1(input_encode_bitrate,
                       user_interface,
                       this,
                       encode_bitrate_cb,
                       encode_config*,
                       encode_c);

  input_mpegts_alignment->value("7");
  input_mpegts_alignment->when(FL_WHEN_CHANGED);
  FL_METHOD_CALLBACK_1(input_mpegts_alignment,
                       user_interface,
                       this,
                       mpegts_alignment_cb,
                       encode_config*,
                       encode_c);

  FL_METHOD_CALLBACK_2(input_rist_address,
                       user_interface,
                       this,
                       input_rist_address_cb,
                       output_config*,
                       output_c,
                       FuncPtr,
                       input_rist_address_funcptr);

  FL_METHOD_CALLBACK_1(btn_preview_input,
                       user_interface,
                       this,
                       btn_preview_input_cb,
                       FuncPtr,
                       preview_src_funcptr);

  FL_METHOD_CALLBACK_1(
      btn_start_encode, user_interface, this, start, FuncPtr, start_funcptr);

  FL_METHOD_CALLBACK_1(
      btn_stop_encode, user_interface, this, stop, FuncPtr, stop_funcptr);

  FL_METHOD_CALLBACK_1(btn_save_settings,
                       user_interface,
                       this,
                       save_settings,
                       FuncPtr,
                       save_settings_funcptr);

  btn_exit->callback([](Fl_Widget*, void* v)
                     { static_cast<user_interface*>(v)->main_window->hide(); },
                     this);

  FL_METHOD_CALLBACK_1(btn_refresh_ndi_devices,
                       user_interface,
                       this,
                       refresh_ndi_devices,
                       FuncPtr,
                       ndi_refresh_funcptr);

  // ---- Receiver / restream control section ----
  // Update the model live as the user types/toggles.
  input_control_address->when(FL_WHEN_CHANGED);
  input_control_token->when(FL_WHEN_CHANGED);
  input_destinations->when(FL_WHEN_CHANGED);

  FL_METHOD_CALLBACK_1(check_receiver_enabled,
                       user_interface,
                       this,
                       receiver_enabled_cb,
                       receiver_control_config*,
                       receiver_c);

  FL_METHOD_CALLBACK_1(input_control_address,
                       user_interface,
                       this,
                       receiver_address_cb,
                       receiver_control_config*,
                       receiver_c);

  FL_METHOD_CALLBACK_1(input_control_token,
                       user_interface,
                       this,
                       receiver_token_cb,
                       receiver_control_config*,
                       receiver_c);

  FL_METHOD_CALLBACK_1(input_destinations,
                       user_interface,
                       this,
                       receiver_destinations_cb,
                       receiver_control_config*,
                       receiver_c);

  // ---- Bridge (LAN) control section (DT-19, DT-21) ----
  // Held so the thread-safe setters can update the model as well as the
  // widgets.
  bridge_config_ptr = bridge_c;

  input_bridge_address->when(FL_WHEN_CHANGED);
  input_bridge_listen->when(FL_WHEN_CHANGED);
  input_bridge_forward->when(FL_WHEN_CHANGED);
  input_bridge_interface->when(FL_WHEN_CHANGED);

  FL_METHOD_CALLBACK_1(input_bridge_address,
                       user_interface,
                       this,
                       bridge_address_cb,
                       bridge_control_config*,
                       bridge_c);

  FL_METHOD_CALLBACK_1(input_bridge_listen,
                       user_interface,
                       this,
                       bridge_listen_cb,
                       bridge_control_config*,
                       bridge_c);

  FL_METHOD_CALLBACK_1(input_bridge_forward,
                       user_interface,
                       this,
                       bridge_forward_cb,
                       bridge_control_config*,
                       bridge_c);

  FL_METHOD_CALLBACK_1(input_bridge_interface,
                       user_interface,
                       this,
                       bridge_interface_cb,
                       bridge_control_config*,
                       bridge_c);

  FL_METHOD_CALLBACK_1(btn_bridge_find,
                       user_interface,
                       this,
                       bridge_find,
                       FuncPtr,
                       bridge_find_funcptr);

  FL_METHOD_CALLBACK_1(btn_bridge_claim,
                       user_interface,
                       this,
                       bridge_claim,
                       FuncPtr,
                       bridge_claim_funcptr);

  FL_METHOD_CALLBACK_1(btn_bridge_calibrate,
                       user_interface,
                       this,
                       bridge_calibrate,
                       FuncPtr,
                       bridge_calibrate_funcptr);
  FL_METHOD_CALLBACK_1(btn_bridge_apply,
                       user_interface,
                       this,
                       bridge_apply,
                       FuncPtr,
                       bridge_apply_funcptr);

  // ---- Hosted (portal) control section ----
  hosted_config_ptr = hosted_c;
  // Held so an allocation can move the encoder's send target from a worker
  // thread without touching the config directly.
  output_config_ptr = output_c;

  input_backplane_url->when(FL_WHEN_CHANGED);

  FL_METHOD_CALLBACK_1(input_backplane_url,
                       user_interface,
                       this,
                       hosted_url_cb,
                       hosted_config*,
                       hosted_c);

  FL_METHOD_CALLBACK_1(btn_hosted_signin,
                       user_interface,
                       this,
                       hosted_sign_in,
                       FuncPtr,
                       hosted_signin_funcptr);

  FL_METHOD_CALLBACK_1(btn_hosted_signout,
                       user_interface,
                       this,
                       hosted_sign_out,
                       FuncPtr,
                       hosted_signout_funcptr);

  FL_METHOD_CALLBACK_1(btn_hosted_allocate,
                       user_interface,
                       this,
                       hosted_allocate,
                       FuncPtr,
                       hosted_allocate_funcptr);
}