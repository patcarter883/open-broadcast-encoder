#include "ui/ui.h"
#include <algorithm>
#include <string>
#include <vector>
#include <functional>
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

Fl_Menu_Item user_interface::menu_choice_input_protocol[] = {
    {.text = " ",
     .shortcut_ = 0,
     .callback_ = 0,
     .user_data_ = (void*)(0),
     .flags = 0,
     .labeltype_ = (uchar)FL_NORMAL_LABEL,
     .labelfont_ = 0,
     .labelsize_ = 14,
     .labelcolor_ = 0},
    {.text = "SDP / RTP",
     .shortcut_ = 0,
     .callback_ = 0,
     .user_data_ = (void*)(1),
     .flags = 0,
     .labeltype_ = (uchar)FL_NORMAL_LABEL,
     .labelfont_ = 0,
     .labelsize_ = 14,
     .labelcolor_ = 0},
    {.text = "NDI",
     .shortcut_ = 0,
     .callback_ = 0,
     .user_data_ = (void*)(2),
     .flags = 0,
     .labeltype_ = (uchar)FL_NORMAL_LABEL,
     .labelfont_ = 0,
     .labelsize_ = 14,
     .labelcolor_ = 0},
     {.text = "MPEGTS",
      .shortcut_ = 0,
      .callback_ = 0,
      .user_data_ = (void*)(3),
      .flags = 0,
      .labeltype_ = (uchar)FL_NORMAL_LABEL,
      .labelfont_ = 0,
      .labelsize_ = 14,
      .labelcolor_ = 0},
     {.text = "Test Pattern",
      .shortcut_ = 0,
      .callback_ = 0,
      .user_data_ = (void*)(4),
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
Fl_Menu_Item* user_interface::select_sdp_input =
    user_interface::menu_choice_input_protocol + 1;
Fl_Menu_Item* user_interface::select_ndi_input =
    user_interface::menu_choice_input_protocol + 2;
Fl_Menu_Item* user_interface::select_mpegts_input =
    user_interface::menu_choice_input_protocol + 3;
Fl_Menu_Item* user_interface::select_test_input =
    user_interface::menu_choice_input_protocol + 4;

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
    main_window = new Fl_Double_Window(1373, 667, "Open Broadcast Encoder");
    main_window->user_data((void*)(this));
    {
      pack = new Fl_Flex(0, 0, 1373, 667);
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
              ndi_options_group->end();
            }  // Fl_Flex* ndi_options_group
            {
              btn_preview_input =
                  new Fl_Button(31, 101, 421, 25, "Preview Input");
            }  // Fl_Button* btn_preview_input
            flx_input->margin(5, 25, 5, 5);
            flx_input->gap(25);
            flx_input->fixed(flx_input->child(0), 25);
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
            o->margin(5, 25, 5, 5);
            o->gap(25);
            o->fixed(o->child(0), 25);
            o->fixed(o->child(1), 25);
            o->fixed(o->child(2), 25);
            o->end();
          }  // Fl_Flex* o
          o->gap(25);
          o->end();
        }  // Fl_Flex* o
        {
          Fl_Flex* o = new Fl_Flex(470, 25, 433, 309, "Output");
          o->box(FL_BORDER_BOX);
          { input_rist_address = new Fl_Input(476, 51, 421, 25, "RIST Address");
            input_rist_address->align(Fl_Align(FL_ALIGN_TOP_LEFT));
          } // Fl_Input* input_rist_address
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
            o->gap(12);
            o->end();
          }  // Fl_Flex* o
          o->margin(5, 25, 5, 5);
          o->gap(25);
          o->fixed(o->child(0), 25);
          o->fixed(o->child(1), 25);
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
          o->margin(5, 0, 5, 0);
          o->fixed(o->child(0), 256);
          o->end();
        }  // Fl_Flex* o
        flx_top->margin(0, 0, 0, 12);
        flx_top->gap(12);
        flx_top->end();
      }  // Fl_Flex* flx_top
      {
        flx_bottom = new Fl_Flex(25, 442, 1323, 200);
        flx_bottom->type(1);
        { transport_log_display = new Fl_Text_Display(25, 442, 662, 200);
        } // Fl_Text_Display* transport_log_display
        { encode_log_display = new Fl_Text_Display(687, 442, 661, 200);
        } // Fl_Text_Display* encode_log_display
        flx_bottom->end();
      }  // Fl_Flex* flx_bottom
      pack->margin(25, 25, 25, 25);
      pack->fixed(pack->child(1), 200);
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
  // Fl::lock();
  transport_log_display->insert(msg.c_str());
  // Fl::unlock();
  // Fl::awake();
}

void user_interface::encode_log_append_cb(const std::string& msg) const
{
  encode_log_display->insert(msg.c_str());
}

void user_interface::encode_log_append(const std::string& msg) const
{
  encode_log_display->insert(msg.c_str());
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

void user_interface::choose_input_protocol(input_config* input_config, FuncPtr refresh_ndi_funcptr)
{
  switch (
      reinterpret_cast<uintptr_t>(choice_input_protocol->mvalue()->user_data()))
  {
    case 1: {
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

    case 2: {
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

    case 3: {
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

    case 4: {
      input_config->selected_input_mode = input_mode::test;
      Fl::lock();
      ndi_options_group->hide();
      sdp_options_group->hide();
      mpegts_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
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

void user_interface::add_ndi_choices(std::vector<char*> choice_names)
{
  Fl::lock();
  std::for_each(
      choice_names.begin(),
      choice_names.end(),
      [&](char* choice_name)
      { choice_ndi_input->add(choice_name, 0, nullptr, choice_name, 0); });

  Fl::unlock();
  Fl::awake();
}

void user_interface::clear_ndi_choices()
{
  Fl::lock();
  choice_ndi_input->clear();
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
  input_config->selected_input = input_listen_port->value();
}

void user_interface::input_rist_address_cb(output_config* output_config, FuncPtr input_rist_address_funcptr)
{
  output_config->address = input_rist_address->value();
  input_rist_address_funcptr();
}

void user_interface::select_codec(encode_config* encode_config)
{
  auto user_data =
      reinterpret_cast<uintptr_t>(choice_codec->mvalue()->user_data());
  encode_config->codec = static_cast<codec>(user_data);
}

void user_interface::select_encoder(encode_config* encode_config)
{
  auto user_data =
      reinterpret_cast<uintptr_t>(choice_encoder->mvalue()->user_data());

  encode_config->encoder = static_cast<encoder>(user_data);
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
                                       FuncPtr start_funcptr,
                                       FuncPtr stop_funcptr,
                                       FuncPtr ndi_refresh_funcptr,
                                       FuncPtr input_rist_address_funcptr,
                                       FuncPtr preview_src_funcptr)
{
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

  FL_METHOD_CALLBACK_1(btn_refresh_ndi_devices,
                       user_interface,
                       this,
                       refresh_ndi_devices,
                       FuncPtr,
                       ndi_refresh_funcptr);
}