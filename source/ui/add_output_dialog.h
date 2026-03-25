#pragma once

#include <FL/Fl_Window.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <string>
#include <memory>
#include <functional>
#include "lib/lib.h"

/**
 * @brief Dialog for adding a new output stream
 * @details Modal dialog for configuring a new output destination
 */
class add_output_dialog : public Fl_Window
{
public:
  using callback_type = std::function<void(const stream_config& config)>;

  add_output_dialog();
  explicit add_output_dialog(int w, int h, const char* title = "Add Output");
  ~add_output_dialog() override;

  /**
   * @brief Set the callback for when dialog is accepted
   * @param callback Function to call with new stream config
   */
  void on_accept(callback_type callback);

  /**
   * @brief Show the dialog as a modal window
   * @return 1 if accepted, 0 if cancelled
   */
  int show_modal();

  /**
   * @brief Get the configured stream config
   * @return stream_config structure
   */
  stream_config get_config() const;

  /**
   * @brief Reset dialog to defaults
   */
  void reset();

private:
  void init_widgets();
  void setup_protocol_choices();

  static void on_cancel(Fl_Widget* w, void* data);
  static void on_add(Fl_Widget* w, void* data);
  static void on_protocol_change(Fl_Widget* w, void* data);

  callback_type m_callback;

  // Protocol selection
  Fl_Choice* m_protocol_choice;

  // Address settings
  Fl_Input* m_address_input;
  Fl_Int_Input* m_port_input;

  // RIST specific
  Fl_Int_Input* m_streams_input;
  Fl_Int_Input* m_bandwidth_input;
  Fl_Int_Input* m_buffer_min_input;
  Fl_Int_Input* m_buffer_max_input;

  // SRT specific
  Fl_Int_Input* m_latency_input;

  // RTMP specific
  Fl_Input* m_stream_key_input;

  // Buttons
  Fl_Button* m_cancel_button;
  Fl_Button* m_add_button;

  // RIST settings group
  Fl_Group* m_rist_group;

  // SRT settings group
  Fl_Group* m_srt_group;

  // RTMP settings group
  Fl_Group* m_rtmp_group;

  transport_protocol m_selected_protocol;
};
