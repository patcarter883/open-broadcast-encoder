#include "add_output_dialog.h"
#include "lib/lib.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <format>

add_output_dialog::add_output_dialog()
  : add_output_dialog(450, 400, "Add Output")
{
}

add_output_dialog::add_output_dialog(int w, int h, const char* title)
  : Fl_Window(w, h, title)
  , m_selected_protocol(transport_protocol::rist)
{
  init_widgets();
  end();
  set_modal();
}

add_output_dialog::~add_output_dialog()
{
}

void add_output_dialog::init_widgets()
{
  constexpr int LABEL_W = 100;
  constexpr int INPUT_W = 150;
  constexpr int PADDING = 10;
  constexpr int ROW_H = 30;
  constexpr int GROUP_H = 150;

  int current_y = PADDING;

  // Protocol choice
  auto* protocol_label = new Fl_Box(PADDING, current_y, LABEL_W, ROW_H, "Protocol:");
  protocol_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  m_protocol_choice = new Fl_Choice(PADDING + LABEL_W, current_y, INPUT_W, ROW_H);
  setup_protocol_choices();
  m_protocol_choice->callback(on_protocol_change, this);

  current_y += ROW_H + PADDING;

  // Address input
  auto* address_label = new Fl_Box(PADDING, current_y, LABEL_W, ROW_H, "Address:");
  address_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  m_address_input = new Fl_Input(PADDING + LABEL_W, current_y, INPUT_W, ROW_H);
  m_address_input->value("127.0.0.1");

  current_y += ROW_H + PADDING;

  // Port input
  auto* port_label = new Fl_Box(PADDING, current_y, LABEL_W, ROW_H, "Port:");
  port_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  m_port_input = new Fl_Int_Input(PADDING + LABEL_W, current_y, INPUT_W, ROW_H);
  m_port_input->value("5000");

  current_y += ROW_H + PADDING;

  // RIST settings group
  m_rist_group = new Fl_Group(PADDING, current_y, w() - PADDING * 2, GROUP_H, "RIST Settings");
  {
    int group_y = current_y + 20;

    auto* streams_label = new Fl_Box(PADDING, group_y, LABEL_W, ROW_H, "Streams:");
    m_streams_input = new Fl_Int_Input(PADDING + LABEL_W, group_y, INPUT_W, ROW_H);
    m_streams_input->value("1");

    group_y += ROW_H + PADDING;

    auto* bw_label = new Fl_Box(PADDING, group_y, LABEL_W, ROW_H, "Bandwidth:");
    m_bandwidth_input = new Fl_Int_Input(PADDING + LABEL_W, group_y, INPUT_W, ROW_H);
    m_bandwidth_input->value("6000");

    group_y += ROW_H + PADDING;

    auto* buf_min_label = new Fl_Box(PADDING, group_y, LABEL_W, ROW_H, "Buffer Min:");
    m_buffer_min_input = new Fl_Int_Input(PADDING + LABEL_W, group_y, INPUT_W, ROW_H);
    m_buffer_min_input->value("245");

    group_y += ROW_H + PADDING;

    auto* buf_max_label = new Fl_Box(PADDING, group_y, LABEL_W, ROW_H, "Buffer Max:");
    m_buffer_max_input = new Fl_Int_Input(PADDING + LABEL_W, group_y, INPUT_W, ROW_H);
    m_buffer_max_input->value("5000");
  }
  m_rist_group->end();

  // SRT settings group (hidden initially)
  m_srt_group = new Fl_Group(PADDING, current_y, w() - PADDING * 2, GROUP_H, "SRT Settings");
  m_srt_group->hide();
  {
    int group_y = current_y + 20;

    auto* latency_label = new Fl_Box(PADDING, group_y, LABEL_W, ROW_H, "Latency:");
    m_latency_input = new Fl_Int_Input(PADDING + LABEL_W, group_y, INPUT_W, ROW_H);
    m_latency_input->value("120");
  }
  m_srt_group->end();

  // RTMP settings group (hidden initially)
  m_rtmp_group = new Fl_Group(PADDING, current_y, w() - PADDING * 2, GROUP_H, "RTMP Settings");
  m_rtmp_group->hide();
  {
    int group_y = current_y + 20;

    auto* key_label = new Fl_Box(PADDING, group_y, LABEL_W, ROW_H, "Stream Key:");
    m_stream_key_input = new Fl_Input(PADDING + LABEL_W, group_y, INPUT_W * 2, ROW_H);
  }
  m_rtmp_group->end();

  current_y += GROUP_H + PADDING;

  // Buttons
  int button_y = current_y;
  int button_w = 80;
  int button_spacing = 10;

  m_cancel_button = new Fl_Button(w() - PADDING - button_w * 2 - button_spacing,
      button_y, button_w, ROW_H, "Cancel");
  m_cancel_button->callback(on_cancel, this);

  m_add_button = new Fl_Button(w() - PADDING - button_w, button_y, button_w, ROW_H, "Add");
  m_add_button->callback(on_add, this);
  Fl::focus(m_add_button);
}

void add_output_dialog::setup_protocol_choices()
{
  m_protocol_choice->add("RIST", nullptr, nullptr);
  m_protocol_choice->add("SRT", nullptr, nullptr);
  m_protocol_choice->add("RTMP", nullptr, nullptr);
  m_protocol_choice->value(0);  // Default to RIST
}

void add_output_dialog::on_accept(callback_type callback)
{
  m_callback = std::move(callback);
}

int add_output_dialog::show_modal()
{
  reset();
  show();
  while (visible()) {
    Fl::wait();
  }
  return 1;  // Returns 1 when dialog is closed
}

stream_config add_output_dialog::get_config() const
{
  stream_config config;

  config.protocol = m_selected_protocol;
  config.address = std::string(m_address_input->value()) + ":" + std::string(m_port_input->value());
  config.streams = safe_parse_int(m_streams_input->value(), 1);
  config.bandwidth = m_bandwidth_input->value();
  config.buffer_min = m_buffer_min_input->value();
  config.buffer_max = m_buffer_max_input->value();
  config.latency = m_latency_input->value();
  config.stream_key = m_stream_key_input->value();

  // Generate unique ID
  static int id_counter = 0;
  config.id = std::format("{}_{}", 
      m_selected_protocol == transport_protocol::rist ? "rist" :
      m_selected_protocol == transport_protocol::srt ? "srt" : "rtmp",
      ++id_counter);

  return config;
}

void add_output_dialog::reset()
{
  m_protocol_choice->value(0);
  m_address_input->value("127.0.0.1");
  m_port_input->value("5000");
  m_streams_input->value("1");
  m_bandwidth_input->value("6000");
  m_buffer_min_input->value("245");
  m_buffer_max_input->value("5000");
  m_latency_input->value("120");
  m_stream_key_input->value("");

  m_rist_group->show();
  m_srt_group->hide();
  m_rtmp_group->hide();
  m_selected_protocol = transport_protocol::rist;
}

void add_output_dialog::on_cancel(Fl_Widget* w, void* data)
{
  auto* dialog = static_cast<add_output_dialog*>(data);
  dialog->hide();
}

void add_output_dialog::on_add(Fl_Widget* w, void* data)
{
  auto* dialog = static_cast<add_output_dialog*>(data);
  
  if (dialog->m_callback) {
    dialog->m_callback(dialog->get_config());
  }
  
  dialog->hide();
}

void add_output_dialog::on_protocol_change(Fl_Widget* w, void* data)
{
  auto* dialog = static_cast<add_output_dialog*>(data);
  int selection = dialog->m_protocol_choice->value();

  dialog->m_rist_group->hide();
  dialog->m_srt_group->hide();
  dialog->m_rtmp_group->hide();

  switch (selection) {
    case 0:  // RIST
      dialog->m_rist_group->show();
      dialog->m_selected_protocol = transport_protocol::rist;
      break;
    case 1:  // SRT
      dialog->m_srt_group->show();
      dialog->m_selected_protocol = transport_protocol::srt;
      break;
    case 2:  // RTMP
      dialog->m_rtmp_group->show();
      dialog->m_selected_protocol = transport_protocol::rtmp;
      break;
  }

  dialog->redraw();
}
