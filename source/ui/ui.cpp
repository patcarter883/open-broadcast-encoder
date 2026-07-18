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
              btn_preview_input =
                  new Fl_Button(31, 101, 421, 25, "Preview Input");
            }  // Fl_Button* btn_preview_input
            flx_input->margin(5, 25, 5, 5);
            flx_input->gap(25);
            flx_input->fixed(flx_input->child(0), 25);
            flx_input->fixed(ndi_options_group, 62);
            flx_input->fixed(btn_preview_input, 25);
            flx_input->end();
          }  // Fl_Flex* flx_input
          {
            Fl_Flex* o = new Fl_Flex(25, 215, 433, 165, "Encode");
            o->box(FL_BORDER_BOX);
            {
              choice_codec = new Fl_Choice(31, 161, 421, 25, "Codec");
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
          o->margin(5, 25, 5, 5);
          o->gap(25);
          o->fixed(o->child(0), 25);  // RIST Address
          o->fixed(o->child(1), 25);  // MPEG-TS Alignment
          o->fixed(o->child(2), 25);  // Start/Stop/Save/Exit button row
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
          {
            check_reencode = new Fl_Check_Button(0, 0, 100, 25, "Reencode");
          }  // Fl_Check_Button* check_reencode
          {
            choice_reencode_codec = new Fl_Choice(0, 0, 110, 25, "Codec");
            choice_reencode_codec->down_box(FL_BORDER_BOX);
            choice_reencode_codec->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            choice_reencode_codec->menu(menu_choice_codec);
          }  // Fl_Choice* choice_reencode_codec
          {
            choice_reencode_encoder = new Fl_Choice(0, 0, 110, 25, "Encoder");
            choice_reencode_encoder->down_box(FL_BORDER_BOX);
            choice_reencode_encoder->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            choice_reencode_encoder->menu(menu_choice_encoder);
          }  // Fl_Choice* choice_reencode_encoder
          {
            input_reencode_bitrate =
                new Fl_Input(0, 0, 110, 25, "Bitrate kbps");
            input_reencode_bitrate->align(Fl_Align(FL_ALIGN_TOP_LEFT));
            input_reencode_bitrate->value("8000");
          }  // Fl_Input* input_reencode_bitrate
          {
            check_upscale = new Fl_Check_Button(0, 0, 120, 25, "Upscale 1440p");
          }  // Fl_Check_Button* check_upscale
          row->gap(10);
          row->end();
        }  // Fl_Flex* row
        {
          input_destinations = new Fl_Multiline_Input(
              25,
              487,
              1323,
              90,
              "Destinations (one per line:  rtmp|rtmps|srt|rist  <url>  [key])");
          input_destinations->align(Fl_Align(FL_ALIGN_TOP_LEFT));
        }  // Fl_Multiline_Input* input_destinations
        flx_receiver->margin(8, 22, 8, 8);
        flx_receiver->gap(22);
        flx_receiver->fixed(flx_receiver->child(0), 25);
        flx_receiver->end();
      }  // Fl_Flex* flx_receiver
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
  Fl::lock();
  transport_log_display->insert(msg.c_str());
  Fl::unlock();
  Fl::awake();
}

void user_interface::encode_log_append(const std::string& msg) const
{
  Fl::lock();
  encode_log_display->insert(msg.c_str());
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
                                           FuncPtr refresh_ndi_funcptr)
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

    default: {
      input_config->selected_input_mode = input_mode::none;
      Fl::lock();
      ndi_options_group->hide();
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

void user_interface::receiver_reencode_cb(receiver_control_config* rc)
{
  rc->reencode = check_reencode->value() != 0;
}

void user_interface::receiver_codec_cb(receiver_control_config* rc)
{
  const Fl_Menu_Item* mv = choice_reencode_codec->mvalue();
  if (mv == nullptr) {
    return;
  }
  rc->video.out_codec =
      static_cast<codec>(reinterpret_cast<uintptr_t>(mv->user_data()));
}

void user_interface::receiver_encoder_cb(receiver_control_config* rc)
{
  const Fl_Menu_Item* mv = choice_reencode_encoder->mvalue();
  if (mv == nullptr) {
    return;
  }
  rc->video.enc =
      static_cast<encoder>(reinterpret_cast<uintptr_t>(mv->user_data()));
}

void user_interface::receiver_bitrate_cb(receiver_control_config* rc)
{
  const char* raw = input_reencode_bitrate->value();
  if (raw == nullptr) {
    return;
  }
  try {
    rc->video.bitrate = std::stoi(raw);
  } catch (...) {  // NOLINT(bugprone-empty-catch) — keep previous value
  }
}

void user_interface::receiver_upscale_cb(receiver_control_config* rc)
{
  rc->video.upscale = check_upscale->value() != 0;
}

void user_interface::receiver_destinations_cb(receiver_control_config* rc)
{
  rc->destinations = parse_destinations(input_destinations->value());
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
                                    const receiver_control_config& receiver_c)
{
  // ---- Input ----
  select_choice_by_userdata(choice_input_protocol,
                            static_cast<long>(input_c.selected_input_mode));
  // Mirror choose_input_protocol()'s option-group visibility for the restored
  // mode so the UI is consistent without synthesising a user click.
  mpegts_options_group->hide();
  sdp_options_group->hide();
  ndi_options_group->hide();
  switch (input_c.selected_input_mode) {
    case input_mode::mpegts:
      input_listen_port->value(input_c.selected_input.c_str());
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
  select_choice_by_userdata(
      choice_bitrate_source,
      static_cast<long>(encode_c.scaling_source.load(std::memory_order_relaxed)));

  // ---- Output ----
  input_rist_address->value(output_c.address.c_str());

  // ---- Receiver / Restream ----
  check_receiver_enabled->value(receiver_c.enabled ? 1 : 0);
  input_control_address->value(
      (receiver_c.control_host + ":" + std::to_string(receiver_c.control_port))
          .c_str());
  input_control_token->value(receiver_c.token.c_str());
  check_reencode->value(receiver_c.reencode ? 1 : 0);
  select_choice_by_userdata(choice_reencode_codec,
                            static_cast<long>(receiver_c.video.out_codec));
  select_choice_by_userdata(choice_reencode_encoder,
                            static_cast<long>(receiver_c.video.enc));
  input_reencode_bitrate->value(std::to_string(receiver_c.video.bitrate).c_str());
  check_upscale->value(receiver_c.video.upscale ? 1 : 0);
  input_destinations->value(format_destinations(receiver_c.destinations).c_str());

  layout();
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
                                       FuncPtr start_funcptr,
                                       FuncPtr stop_funcptr,
                                       FuncPtr ndi_refresh_funcptr,
                                       FuncPtr input_rist_address_funcptr,
                                       FuncPtr preview_src_funcptr,
                                       FuncPtr scaling_source_changed_funcptr,
                                       FuncPtr save_settings_funcptr)
{
  main_window->callback([](Fl_Widget* w, void*) { w->hide(); });

  transport_log_display->buffer(transport_log_buffer);
  encode_log_display->buffer(encode_log_buffer);

  FL_METHOD_CALLBACK_2(choice_input_protocol,
                       user_interface,
                       this,
                       choose_input_protocol,
                       input_config*,
                       input_c,
                       FuncPtr,
                       ndi_refresh_funcptr);

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

  btn_exit->callback([](Fl_Widget*, void* v) {
    static_cast<user_interface*>(v)->main_window->hide();
  }, this);

  FL_METHOD_CALLBACK_1(btn_refresh_ndi_devices,
                       user_interface,
                       this,
                       refresh_ndi_devices,
                       FuncPtr,
                       ndi_refresh_funcptr);

  // ---- Receiver / restream control section ----
  // Default the reencode codec/encoder choices so the model matches the
  // displayed selection (h264 / software) before the user touches them.
  choice_reencode_codec->value(0);     // h264 (index 0 of menu_choice_codec)
  choice_reencode_encoder->value(3);   // Software (index 3 of menu_choice_encoder)

  // Update the model live as the user types/toggles.
  input_control_address->when(FL_WHEN_CHANGED);
  input_control_token->when(FL_WHEN_CHANGED);
  input_reencode_bitrate->when(FL_WHEN_CHANGED);
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

  FL_METHOD_CALLBACK_1(check_reencode,
                       user_interface,
                       this,
                       receiver_reencode_cb,
                       receiver_control_config*,
                       receiver_c);

  FL_METHOD_CALLBACK_1(choice_reencode_codec,
                       user_interface,
                       this,
                       receiver_codec_cb,
                       receiver_control_config*,
                       receiver_c);

  FL_METHOD_CALLBACK_1(choice_reencode_encoder,
                       user_interface,
                       this,
                       receiver_encoder_cb,
                       receiver_control_config*,
                       receiver_c);

  FL_METHOD_CALLBACK_1(input_reencode_bitrate,
                       user_interface,
                       this,
                       receiver_bitrate_cb,
                       receiver_control_config*,
                       receiver_c);

  FL_METHOD_CALLBACK_1(check_upscale,
                       user_interface,
                       this,
                       receiver_upscale_cb,
                       receiver_control_config*,
                       receiver_c);

  FL_METHOD_CALLBACK_1(input_destinations,
                       user_interface,
                       this,
                       receiver_destinations_cb,
                       receiver_control_config*,
                       receiver_c);
}