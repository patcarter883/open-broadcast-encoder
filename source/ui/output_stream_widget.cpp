#include "output_stream_widget.h"
#include <FL/Fl_Output.H>
#include <format>

output_stream_widget::output_stream_widget(int x, int y, int w, int h, const std::string& stream_id)
  : Fl_Group(x, y, w, h)
  , m_stream_id(stream_id)
  , m_protocol("RIST")
  , m_address("127.0.0.1:5000")
{
  init_widgets();
  end();
}

output_stream_widget::~output_stream_widget()
{
}

void output_stream_widget::init_widgets()
{
  constexpr int LABEL_W = 70;
  constexpr int OUTPUT_W = 80;
  constexpr int STATUS_W = 20;
  constexpr int BUTTON_W = 60;
  constexpr int PADDING = 5;
  constexpr int ROW_H = 25;

  int current_x = x() + PADDING;
  int current_y = y() + PADDING;

  // Protocol box
  m_protocol_box = new Fl_Box(current_x, current_y, LABEL_W, ROW_H, m_protocol.c_str());
  m_protocol_box->box(FL_UP_BOX);
  m_protocol_box->align(FL_ALIGN_CENTER);

  current_x += LABEL_W + PADDING;

  // Address box
  m_address_box = new Fl_Box(current_x, current_y, LABEL_W * 2, ROW_H, m_address.c_str());
  m_address_box->box(FL_UP_BOX);
  m_address_box->align(FL_ALIGN_CENTER);

  current_x += LABEL_W * 2 + PADDING;

  // Bandwidth output
  m_bandwidth_output = new Fl_Output(current_x, current_y, OUTPUT_W, ROW_H);
  m_bandwidth_output->label("BW (kbps)");
  m_bandwidth_output->value("0");

  current_x += OUTPUT_W + PADDING;

  // Quality output
  m_quality_output = new Fl_Output(current_x, current_y, OUTPUT_W, ROW_H);
  m_quality_output->label("Quality");
  m_quality_output->value("100");

  current_x += OUTPUT_W + PADDING;

  // RTT output
  m_rtt_output = new Fl_Output(current_x, current_y, OUTPUT_W, ROW_H);
  m_rtt_output->label("RTT");
  m_rtt_output->value("0");

  current_x += OUTPUT_W + PADDING;

  // Packets output
  m_packets_output = new Fl_Output(current_x, current_y, OUTPUT_W, ROW_H);
  m_packets_output->label("Packets");
  m_packets_output->value("0");

  current_x += OUTPUT_W + PADDING;

  // Status indicator
  m_status_indicator = new Fl_Box(current_x, current_y, STATUS_W, ROW_H, "@bullet");
  m_status_indicator->color(FL_RED);
  m_status_indicator->labelcolor(FL_WHITE);

  current_x += STATUS_W + PADDING;

  // Remove button
  m_remove_button = new Fl_Button(current_x, current_y, BUTTON_W, ROW_H, "Remove");
  m_remove_button->callback([](Fl_Widget* w, void* data) {
    auto* self = static_cast<output_stream_widget*>(data);
    if (self->m_remove_callback) {
      self->m_remove_callback(*self);
    }
  }, this);
}

void output_stream_widget::update_stats(int64_t bandwidth, double quality, int32_t rtt,
    int64_t packets_sent, bool connected)
{
  // Note: FLTK UI updates must be done from main thread
  // This method is called from stats callbacks which may be on different threads

  if (Fl::lock()) {
    m_bandwidth_output->value(std::to_string(bandwidth / 1000).c_str());
    m_quality_output->value(std::to_string(static_cast<int>(quality)).c_str());
    m_rtt_output->value(std::to_string(rtt).c_str());
    m_packets_output->value(std::to_string(packets_sent).c_str());

    set_connected(connected);

    Fl::unlock();
    Fl::awake();
  }
}

void output_stream_widget::set_stream_id(const std::string& id)
{
  m_stream_id = id;
}

std::string output_stream_widget::get_stream_id() const
{
  return m_stream_id;
}

void output_stream_widget::set_protocol(const std::string& protocol)
{
  m_protocol = protocol;
  if (m_protocol_box) {
    m_protocol_box->label(protocol.c_str());
  }
}

void output_stream_widget::set_address(const std::string& address)
{
  m_address = address;
  if (m_address_box) {
    m_address_box->label(address.c_str());
  }
}

void output_stream_widget::set_remove_callback(callback_type callback)
{
  m_remove_callback = std::move(callback);
}

void output_stream_widget::set_connected(bool connected)
{
  if (m_status_indicator) {
    m_status_indicator->color(connected ? FL_GREEN : FL_RED);
    m_status_indicator->redraw();
  }
}
