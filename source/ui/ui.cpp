// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <cstdlib>
#include <format>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ui/ui.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Flex.H>
#include <FL/Fl_Grid.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Text_Display.H>
#include <stdint.h>

#include "FL/fl_callback_macros.H"
#include "lib/ndi_options.h"
#include "net/adapters.h"

namespace
{
// Select the menu item whose user_data encodes `value`. The encoder menu order
// (AMD, NVENC, QSV, Software) does NOT match the encoder enum order, so a saved
// choice must be restored by matching user_data — never by using the enum as a
// menu index.
void select_choice_by_userdata(Fl_Choice* choice, std::intptr_t value)
{
  const Fl_Menu_Item* menu = choice->menu();
  if (menu == nullptr) {
    return;
  }
  for (int i = 0; menu[i].text != nullptr; ++i) {
    if (reinterpret_cast<std::intptr_t>(menu[i].user_data()) == value) {
      choice->value(i);
      return;
    }
  }
}

// ---- The uplink picker -----------------------------------------------------
//
// A modal window rather than more rows in the Output pane: the uplink list
// grows with the hardware attached to the machine, and the pane's rows are
// fixed height. It edits output_config::uplinks in place, on the UI thread, so
// Save Settings persists whatever is on screen.
class uplink_dialog : public Fl_Window
{
public:
  uplink_dialog(std::vector<uplink::config>& uplinks,
                std::vector<net::adapter> adapters)
      : Fl_Window(600, 330, "Bond uplinks")
      , uplinks_(uplinks)
      , adapters_(std::move(adapters))
  {
    auto* caption = new Fl_Box(20, 16, 95, 25, "Adapter");
    caption->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    choice_ = new Fl_Choice(120, 16, 340, 25);
    for (const net::adapter& a : adapters_) {
      // Only an adapter that is up and carries an IPv4 can hold a leg: the
      // other two states are shown as unusable rather than offered and refused
      // later.
      if (!a.up || a.ipv4.empty()) {
        continue;
      }
      choice_->add(std::format("{}  ({})", a.name, a.ipv4).c_str());
      names_.push_back(a.name);
    }
    if (!names_.empty()) {
      choice_->value(0);
    }

    auto_ = new Fl_Check_Button(120, 53, 130, 25, "Auto weight");
    auto_->value(1);
    auto_->callback(toggle_cb, this);

    weight_ = new Fl_Input(270, 53, 70, 25);
    weight_->value("100");
    weight_->deactivate();

    auto* add = new Fl_Button(360, 53, 90, 25, "Add");
    add->callback(add_cb, this);
    auto* remove = new Fl_Button(460, 53, 100, 25, "Remove");
    remove->callback(remove_cb, this);

    auto* hint = new Fl_Box(20,
                            80,
                            560,
                            20,
                            "One leg per adapter, all to the same host and "
                            "port. Empty sends the single default-route leg.");
    hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    hint->labelsize(11);

    browser_ = new Fl_Hold_Browser(20, 104, 560, 170);

    auto* done = new Fl_Button(480, 286, 100, 28, "Done");
    done->callback(done_cb, this);

    end();
    set_modal();
    refresh();
  }

private:
  std::vector<uplink::config>& uplinks_;
  std::vector<net::adapter> adapters_;
  // Adapter names in menu order: the choice only stores a label, and the name
  // is what an uplink must remember.
  std::vector<std::string> names_;
  Fl_Choice* choice_ = nullptr;
  Fl_Check_Button* auto_ = nullptr;
  Fl_Input* weight_ = nullptr;
  Fl_Hold_Browser* browser_ = nullptr;

  void refresh()
  {
    browser_->clear();
    for (const uplink::config& u : uplinks_) {
      browser_->add(std::format("{}   {}",
                                u.name,
                                u.weight_is_auto
                                    ? std::string("auto")
                                    : std::format("weight {}", u.fixed_weight))
                        .c_str());
    }
  }

  void add()
  {
    if (names_.empty() || choice_->value() < 0) {
      return;
    }
    const std::string& name =
        names_[static_cast<std::size_t>(choice_->value())];
    for (const uplink::config& u : uplinks_) {
      // Two peers on one adapter is not a bond: it splits the payload across
      // the same device and halves the recovery budget for nothing.
      if (u.name == name) {
        return;
      }
    }
    uplink::config u;
    u.name = name;
    u.weight_is_auto = auto_->value() != 0;
    const int fixed = std::atoi(weight_->value());
    u.fixed_weight = fixed < 1 ? 1 : fixed;
    uplinks_.push_back(u);
    refresh();
  }

  void remove()
  {
    const int line = browser_->value();  // 1-based, 0 when nothing is selected
    if (line < 1 || static_cast<std::size_t>(line) > uplinks_.size()) {
      return;
    }
    uplinks_.erase(uplinks_.begin() + (line - 1));
    refresh();
  }

  static void add_cb(Fl_Widget*, void* self)
  {
    static_cast<uplink_dialog*>(self)->add();
  }
  static void remove_cb(Fl_Widget*, void* self)
  {
    static_cast<uplink_dialog*>(self)->remove();
  }
  static void toggle_cb(Fl_Widget*, void* self)
  {
    auto* dialog = static_cast<uplink_dialog*>(self);
    if (dialog->auto_->value() != 0) {
      dialog->weight_->deactivate();
    } else {
      dialog->weight_->activate();
    }
  }
  static void done_cb(Fl_Widget*, void* self)
  {
    static_cast<uplink_dialog*>(self)->hide();
  }
};
}  // namespace

// The widget tree is generated: ui.fld -> ui_widgets.{h,cpp} (see ui.h). The
// base constructor has already created every widget by the time this runs, so
// this body carries only what the design file cannot express.
user_interface::user_interface()
{
  // A floor for the window, not just a size. The panels' rows are fixed-height,
  // so shrinking the window past the height of the stack makes those rows
  // overlap instead of reflowing. fluid has no property for this, so it lives
  // here.
  main_window->size_range(1600, 1010);
  init_link_peer_rows();
}

// The per-peer leg rows (4c). Created here, on the UI thread, once: the RIST
// stats thread only sets values and shows/hides them, so no widget is ever
// allocated from a callback thread.
void user_interface::init_link_peer_rows()
{
  constexpr int kMaxLegs = 6;
  constexpr int kRowHeight = 22;
  flx_peers->begin();
  for (int i = 0; i < kMaxLegs; ++i) {
    auto* row = new Fl_Output(0, 0, 1, kRowHeight);
    row->box(FL_FLAT_BOX);
    row->textfont(FL_COURIER);
    row->textsize(12);
    row->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    row->hide();
    flx_peers->add(row);
    link_peer_rows.push_back(row);
  }
  flx_peers->end();
}

void user_interface::edit_uplinks()
{
  if (output_config_ptr == nullptr) {
    return;
  }
  // The adapter set is read here, not at construction: a modem that appeared
  // after launch is offered the next time the picker is opened.
  uplink_dialog dialog(output_config_ptr->uplinks, net::list());
  // Over the pane that opened it, not at the screen origin.
  dialog.position(main_window->x() + (main_window->w() - dialog.w()) / 2,
                  main_window->y() + (main_window->h() - dialog.h()) / 2);
  dialog.show();
  // FLTK maps a modal window and returns; the nested wait is what keeps this
  // callback (and the pane behind it) responsive while the dialog is up.
  while (dialog.shown() != 0) {
    Fl::wait();
  }
  refresh_uplinks_label();
}

void user_interface::refresh_uplinks_label()
{
  if (btn_uplinks == nullptr || output_config_ptr == nullptr) {
    return;
  }
  const std::size_t legs = output_config_ptr->uplinks.size();
  if (legs == 0) {
    btn_uplinks->label("Uplinks");
  } else {
    btn_uplinks->copy_label(std::format("Uplinks ({})", legs).c_str());
  }
  btn_uplinks->redraw();
}

void user_interface::set_link_peers(const std::vector<std::string>& rows)
{
  const std::size_t shown = std::min(rows.size(), link_peer_rows.size());
  for (std::size_t i = 0; i < link_peer_rows.size(); ++i) {
    Fl_Output* row = link_peer_rows[i];
    if (i < shown) {
      row->value(rows[i].c_str());
      row->show();
    } else if (row->visible() != 0) {
      row->hide();
    }
  }
  if (shown == link_peer_rows_shown) {
    return;
  }
  link_peer_rows_shown = shown;
  // The panel carries the label plus one line per leg, and gives the height
  // back to the log panes when a bond drops to a single leg.
  constexpr int kChrome = 26;
  constexpr int kRowPitch = 24;
  const int height =
      shown == 0 ? 55 : kChrome + kRowPitch * static_cast<int>(shown);
  flx_col_right->fixed(flx_peers, height);
  flx_col_right->layout();
  flx_peers->layout();
}

void user_interface::show(int argc, char** argv)
{
  main_window->show(argc, argv);
  // Every container in the tree is a flex, and Fl_Flex lays its children out on
  // resize -- it does not do it when the window is mapped. So without this the
  // first frame is whatever geometry the generated constructor happened to
  // carry, which is whatever coords fluid wrote last, and a panel resized in
  // the design file shows its rows at the old offsets. Walking the tree here
  // makes the .fld's margin/gap/fixed properties the only thing that decides
  // the layout, which is the point of generating the tree from it.
  layout();
}

namespace
{
// Lay out every flex in the tree, outermost first.
//
// A flex positions its OWN children when its layout() runs -- laying out just
// the outer containers leaves the panels and the rows inside them (which are
// flexes too) holding whatever geometry the generated constructor gave them.
// Panels and rows are mostly unnamed in the design file, so this walks the tree
// rather than naming each one.
void layout_flex_tree(Fl_Widget* w)
{
  if (Fl_Flex* flex = dynamic_cast<Fl_Flex*>(w)) {
    flex->layout();
  }
  if (Fl_Group* group = w->as_group()) {
    for (int i = 0; i < group->children(); ++i) {
      layout_flex_tree(group->child(i));
    }
  }
}
}  // namespace

void user_interface::layout()
{
  layout_flex_tree(main_window);
}

void user_interface::append_log(Fl_Text_Display* display,
                               std::vector<std::string>& pending,
                               const std::string& msg) const
{
  // M1.9: no stream key / token / PSK material ever reaches the log panes.
  const std::string clean = secrets::redact(msg);
  std::lock_guard<std::mutex> guard(log_mutex);
  if (!logs_ready || display == nullptr) {
    if (pending.size() < kMaxPendingLogLines) {
      pending.push_back(clean);
    }
    return;
  }
  Fl::lock();
  display->insert(clean.c_str());
  Fl::unlock();
  Fl::awake();
}

void user_interface::flush_pending_logs()
{
  std::vector<std::string> transport;
  std::vector<std::string> encode;
  {
    std::lock_guard<std::mutex> guard(log_mutex);
    transport.swap(pending_transport_log);
    encode.swap(pending_encode_log);
    logs_ready = true;
  }
  for (const std::string& line : transport) {
    transport_log_append(line);
  }
  for (const std::string& line : encode) {
    encode_log_append(line);
  }
}

void user_interface::transport_log_append(const std::string& msg) const
{
  append_log(transport_log_display, pending_transport_log, msg);
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
    refresh_end_session();
  }
  unlock();
  Fl::awake();
}

void user_interface::set_input_status(const std::string& text)
{
  lock();
  input_status_output->value(text.c_str());
  input_status_output->redraw();
  unlock();
  Fl::awake();
}

void user_interface::encode_log_append(const std::string& msg) const
{
  append_log(encode_log_display, pending_encode_log, msg);
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

void user_interface::request_close()
{
  if (main_window != nullptr) {
    main_window->hide();
  }
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
      // Without the runtime there is no monitor to refresh; the group's status
      // line already says why the picker is inactive.
      if (ndi_enabled) {
        refresh_ndi_funcptr();
      }
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
      // Keep the model in step with the field: a programmatic value() does not
      // fire the callback, so without this the save writes an empty port and
      // the reader falls back to its own default while the field shows another.
      input_config->listen_port = input_listen_port->value();
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
      input_config->listen_port = input_listen_port->value();
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

void user_interface::set_ndi_available(bool available)
{
  const ndi_options_state state = ndi_options_state_for(available);
  ndi_enabled = state.selectable;
  lock();
  if (state.selectable) {
    choice_ndi_input->activate();
  } else {
    choice_ndi_input->deactivate();
  }
  if (state.refreshable) {
    btn_refresh_ndi_devices->activate();
  } else {
    btn_refresh_ndi_devices->deactivate();
  }
  ndi_state_output->value(std::string(state.status).c_str());
  // Nothing to say while the runtime is present: the row then holds the refresh
  // button alone, at the width it has always had.
  if (state.status.empty()) {
    ndi_state_output->hide();
  } else {
    ndi_state_output->show();
  }
  ndi_state_output->redraw();
  layout();
  unlock();
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
  input_config->listen_port = raw;
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

void user_interface::encoder_name_cb(std::string* encoder_name)
{
  const char* raw = input_encoder_name->value();
  *encoder_name = (raw != nullptr) ? raw : "";
  if (encoder_name->size() > 64) {
    encoder_name->resize(64);
  }
  const std::string shown =
      encoder_name->empty() ? default_encoder_name() : *encoder_name;
  main_window->label(("Open Broadcast Encoder - " + shown).c_str());
}

// ---- Bridge (LAN) control callbacks ---------------------------------------

void user_interface::wan_encryption_cb(bridge_control_config* bridge_config)
{
  bridge_config->encrypt_wan_leg = check_wan_encryption->value() != 0;
}

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

void user_interface::bridge_miface_cb(bridge_control_config* bridge_config)
{
  bridge_config->listen_miface = input_bridge_listen_interface->value();
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

// End Session is destructive and billable, so it is a two-step action: the
// first click only arms it, and a 5 s timer reverts the label. That is the
// whole guard -- there is no modal: the operator has already been told the
// session is held by the status line and the log. The deallocate itself runs
// on a tracked worker in main.cpp, because it blocks on HTTP.
void user_interface::hosted_end_session(FuncPtr end_session_funcptr)
{
  lock();
  if (!end_session_armed) {
    end_session_armed = true;
    btn_hosted_end_session->label("End Session?");
    btn_hosted_end_session->redraw();
    Fl::add_timeout(5.0, end_session_timeout_cb, this);
    unlock();
    Fl::awake();
    return;
  }
  // Second click inside the window: run it.
  end_session_armed = false;
  Fl::remove_timeout(end_session_timeout_cb, this);
  btn_hosted_end_session->label("End Session");
  btn_hosted_end_session->deactivate();
  btn_hosted_end_session->redraw();
  unlock();
  Fl::awake();
  if (end_session_funcptr != nullptr) {
    end_session_funcptr();
  }
}

void user_interface::end_session_timeout_cb(void* data)
{
  auto* self = static_cast<user_interface*>(data);
  if (self == nullptr) {
    return;
  }
  self->lock();
  self->end_session_armed = false;
  self->btn_hosted_end_session->label("End Session");
  self->btn_hosted_end_session->redraw();
  self->refresh_end_session();
  self->unlock();
  Fl::awake();
}

void user_interface::refresh_end_session()
{
  // A held session is the precondition; a live feed forbids ending it, because
  // that would tear the receiver down under the operator's stream.
  if (hosted_session_held && !btn_stop_encode->active()) {
    btn_hosted_end_session->activate();
    return;
  }
  btn_hosted_end_session->deactivate();
  if (end_session_armed) {
    // The timer stays queued and no-ops; removing it here would mean touching
    // FLTK's timer list from a worker thread, which set_hosted_session_held()
    // may be on.
    end_session_armed = false;
    btn_hosted_end_session->label("End Session");
    btn_hosted_end_session->redraw();
  }
}

void user_interface::set_hosted_session_held(bool held)
{
  lock();
  hosted_session_held = held;
  refresh_end_session();
  unlock();
  Fl::awake();
}

void user_interface::start(void (*start_funcptr)())
{
  if (input_cfg_ptr != nullptr &&
      input_cfg_ptr->selected_input_mode == input_mode::ndi && !ndi_enabled) {
    // A saved project naming NDI on a host without the runtime: no ndisrc can
    // exist, so the pipeline would carry nothing. Say why and leave the model
    // and the Start/Stop buttons as they were.
    set_input_status("NDI unavailable - install the NDI runtime");
    encode_log_append("NDI input unavailable: the NDI runtime is not installed on "
                      "this machine. Install it and restart the encoder, or pick "
                      "another input.\n");
    return;
  }

  lock();
  btn_start_encode->deactivate();
  btn_stop_encode->activate();
  // Ending the session under a live feed is refused: the button only comes
  // alive again once Stop has taken the stream down.
  refresh_end_session();
  unlock();
  // Same discipline as stop() below. The event loop owns the lock permanently
  // -- init_ui() takes it once and Fl::run() only releases it while waiting --
  // so the unlock() above removes this method's count while the UI thread still
  // holds one. start_funcptr() tears the preview down and joins its worker, and
  // that worker reports through encode_log(), which takes this same lock;
  // joining while the UI thread holds it deadlocks the application. Release the
  // loop's count for the call and reclaim it before returning to the event
  // loop.
  Fl::unlock();
  start_funcptr();
  Fl::lock();
}

void user_interface::stop(void (*stop_funcptr)())
{
  lock();
  btn_start_encode->activate();
  btn_stop_encode->deactivate();
  // A held session makes End Session actionable again: Stop ends the feed, not
  // the session (main.cpp's stop() states that in the log and the status line).
  refresh_end_session();
  unlock();
  // The event loop owns the lock permanently: init_ui() takes it once and
  // Fl::run() only releases it while waiting, so the unlock() above removes
  // this method's count while the UI thread still holds one. stop_funcptr()
  // joins the sender and reader threads, and both report progress through
  // setters that take this same lock -- joining while holding it deadlocks the
  // application, which is the Stop Encode freeze. Release the loop's count for
  // the teardown and reclaim it before returning to the event loop.
  Fl::unlock();
  stop_funcptr();
  Fl::lock();
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
                                    const std::string& encoder_name,
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
      input_listen_port->value(input_c.listen_port.c_str());
      mpegts_options_group->show();
      break;
    case input_mode::raw_local:
      // The OBS raw output's own default. Never the NDI device name: that is
      // what this field used to show, which is not a port at all.
      input_listen_port->value(input_c.listen_port.c_str());
      mpegts_options_group->show();
      // Say what has to happen for this input to receive anything. The listener
      // opens with the pipeline, so before Start the port is closed and an
      // operator staring at OBS has no way to tell that from a dead ingest.
      input_status_output->value(
          std::string("not started - press Start Encode to "
                      "listen on ")
              .append(input_c.listen_port)
              .c_str());
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
      input_listen_port->value(input_c.listen_port.c_str());
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
  refresh_uplinks_label();

  // ---- Instance identity ----
  const std::string shown_name =
      encoder_name.empty() ? default_encoder_name() : encoder_name;
  input_encoder_name->value(encoder_name.c_str());
  main_window->label(("Open Broadcast Encoder - " + shown_name).c_str());

  // ---- Bridge (LAN) ----
  // Written directly rather than through the setters: this runs once on the
  // main thread before show(), and the setters take the FLTK lock.
  input_bridge_address->value(bridge_c.address.c_str());
  input_bridge_listen->value(bridge_c.listen_url.c_str());
  input_bridge_listen_interface->value(bridge_c.listen_miface.c_str());
  input_bridge_forward->value(bridge_c.forward_to.c_str());
  input_bridge_interface->value(bridge_c.interface_name.c_str());
  check_wan_encryption->value(bridge_c.encrypt_wan_leg ? 1 : 0);
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
  // An empty url CLEARS the target. A half-applied allocation still leaves a
  // live session behind, and leaving the previous destination in the field
  // would send the next Start Encode to a session that no longer exists.
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
    } else if (url.empty()) {
      output_config_ptr->host.clear();
      // The key belongs to the destination: clearing one clears the other, so a
      // later manual address cannot inherit a session's secret and send an
      // encrypted stream to a listener that is clear.
      output_config_ptr->psk.clear();
      output_config_ptr->psk_aes = 0;
    }
    input_rist_address->value(url.c_str());
  }
  unlock();
  Fl::awake();
}

void user_interface::set_session_psk(const std::string& psk, int psk_aes)
{
  // Register first: from this point every log line is masked, including the
  // ones the sender emits while it is being built.
  if (!psk.empty()) {
    secrets::register_secret(psk);
  }
  lock();
  if (output_config_ptr != nullptr) {
    output_config_ptr->psk = psk;
    output_config_ptr->psk_aes = psk_aes;
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
                                       std::string* encoder_name,
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
                                       FuncPtr hosted_end_session_funcptr,
                                       FuncPtr refresh_capture_funcptr)
{
  main_window->callback([](Fl_Widget* w, void*) { w->hide(); });

  transport_log_display->buffer(transport_log_buffer);
  encode_log_display->buffer(encode_log_buffer);
  input_cfg_ptr = input_c;
  flush_pending_logs();

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

  btn_uplinks->callback([](Fl_Widget*, void* v)
                        { static_cast<user_interface*>(v)->edit_uplinks(); },
                        this);

  btn_exit->callback([](Fl_Widget*, void* v)
                     { static_cast<user_interface*>(v)->main_window->hide(); },
                     this);

  FL_METHOD_CALLBACK_1(btn_refresh_ndi_devices,
                       user_interface,
                       this,
                       refresh_ndi_devices,
                       FuncPtr,
                       ndi_refresh_funcptr);

  // ---- Instance identity ----
  input_encoder_name->when(FL_WHEN_CHANGED);
  FL_METHOD_CALLBACK_1(input_encoder_name,
                       user_interface,
                       this,
                       encoder_name_cb,
                       std::string*,
                       encoder_name);

  // ---- Bridge (LAN) control section (DT-19, DT-21) ----
  // Held so the thread-safe setters can update the model as well as the
  // widgets.
  bridge_config_ptr = bridge_c;

  input_bridge_address->when(FL_WHEN_CHANGED);
  input_bridge_listen->when(FL_WHEN_CHANGED);
  input_bridge_listen_interface->when(FL_WHEN_CHANGED);
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

  FL_METHOD_CALLBACK_1(input_bridge_listen_interface,
                       user_interface,
                       this,
                       bridge_miface_cb,
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

  FL_METHOD_CALLBACK_1(check_wan_encryption,
                       user_interface,
                       this,
                       wan_encryption_cb,
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

  FL_METHOD_CALLBACK_1(btn_hosted_end_session,
                       user_interface,
                       this,
                       hosted_end_session,
                       FuncPtr,
                       hosted_end_session_funcptr);
}