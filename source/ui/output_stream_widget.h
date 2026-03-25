#pragma once

#include <functional>
#include <string>
#include <memory>
#include <cstdint>
#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Output.H>

/**
 * @brief FLTK widget for displaying a single output stream
 * @details Shows stream info, status, and controls for one output destination
 */
class output_stream_widget : public Fl_Group
{
public:
  using callback_type = std::function<void(output_stream_widget& widget)>;

  output_stream_widget(int x, int y, int w, int h, const std::string& stream_id = "");
  ~output_stream_widget() override;

  /**
   * @brief Update the stream display with new statistics
   * @param bandwidth Current bandwidth in bps
   * @param quality Link quality percentage
   * @param rtt Round-trip time in ms
   * @param packets_sent Total packets sent
   * @param connected Connection status
   */
  void update_stats(int64_t bandwidth, double quality, int32_t rtt,
      int64_t packets_sent, bool connected);

  /**
   * @brief Set the stream ID
   * @param id Stream identifier
   */
  void set_stream_id(const std::string& id);

  /**
   * @brief Get the stream ID
   * @return Stream identifier
   */
  std::string get_stream_id() const;

  /**
   * @brief Set the protocol type display
   * @param protocol Protocol name string
   */
  void set_protocol(const std::string& protocol);

  /**
   * @brief Set the address display
   * @param address Stream address
   */
  void set_address(const std::string& address);

  /**
   * @brief Set the remove button callback
   * @param callback Function to call when remove is clicked
   */
  void set_remove_callback(callback_type callback);

  /**
   * @brief Set connection status
   * @param connected true if connected
   */
  void set_connected(bool connected);

private:
  void init_widgets();

  std::string m_stream_id;
  std::string m_protocol;
  std::string m_address;

  Fl_Box* m_protocol_box;
  Fl_Box* m_address_box;
  Fl_Output* m_bandwidth_output;
  Fl_Output* m_quality_output;
  Fl_Output* m_rtt_output;
  Fl_Output* m_packets_output;
  Fl_Box* m_status_indicator;
  Fl_Button* m_remove_button;

  callback_type m_remove_callback;
};
