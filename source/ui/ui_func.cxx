#include "ui.h"

#include "FL/fl_callback_macros.H"
#include "common.h"

void user_interface::choose_input_protocol(input_config& input_config)
{
  switch (
      reinterpret_cast<uintptr_t>(choice_input_protocol->mvalue()->user_data()))
  {
    case 1: {
      input_config.selected_input_mode = input_mode::sdp;
      Fl::lock();
      sdp_options_group->show();
      ndi_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
      break;
    }

    case 2: {
      input_config.selected_input_mode = input_mode::ndi;
      Fl::lock();
      ndi_options_group->show();
      sdp_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
      break;
    }

    default: {
      input_config.selected_input_mode = input_mode::none;
      Fl::lock();
      ndi_options_group->hide();
      sdp_options_group->hide();
      layout();
      Fl::unlock();
      Fl::awake();
    }
  }
}

void user_interface::choose_ndi_input(input_config& input_config)
{
  input_config.selected_input =
      static_cast<char*>(choice_ndi_input->mvalue()->user_data());
}

void user_interface::select_codec(encode_config& encode_config)
{
  auto user_data =
      reinterpret_cast<uintptr_t>(choice_codec->mvalue()->user_data());
  encode_config.codec = static_cast<codec>(user_data);
}

void user_interface::select_encoder(encode_config& encode_config)
{
  auto user_data =
      reinterpret_cast<uintptr_t>(choice_encoder->mvalue()->user_data());

  encode_config.encoder = static_cast<encoder>(user_data);
}

void user_interface::init_ui_callbacks(user_interface& self,
                                       input_config& input_c,
                                       encode_config& encode_c)
{
  log_display->buffer(log_buffer);

  FL_METHOD_CALLBACK_1(choice_input_protocol,
                       user_interface,
                       &self,
                       choose_input_protocol,
                       input_config,
                       input_c);

  FL_METHOD_CALLBACK_1(choice_ndi_input,
                       user_interface,
                       &self,
                       choose_ndi_input,
                       input_config,
                       input_c);

  FL_METHOD_CALLBACK_1(choice_codec,
                       user_interface,
                       &self,
                       select_codec,
                       encode_config,
                       encode_c);

  FL_METHOD_CALLBACK_1(choice_encoder,
                       user_interface,
                       &self,
                       select_encoder,
                       encode_config,
                       encode_c);

  //   FL_METHOD_CALLBACK_0(btn_preview_input,
  //                        []
  //                        {
  //                          switch (app.selected_input_mode) {
  //                            case input_mode::sdp: {
  //                              break;
  //                            }

  //                            case input_mode::ndi: {
  //                              ndi->preview();
  //                              break;
  //                            }

  //                            case input_mode::none: {
  //                              break;
  //                            }
  //                          }
  //                        });

  //   FL_METHOD_CALLBACK_0(btn_start_encode,
  //                        []
  //                        {
  //                          run();
  //                          Fl::awake(
  //                              [](void*)
  //                              {
  //                                btn_start_encode->deactivate();
  //                                btn_stop_encode->activate();
  //                              });
  //                        });

  //   FL_METHOD_CALLBACK_0(btn_stop_encode,
  //                        []
  //                        {
  //                          stop();
  //                          Fl::awake(
  //                              [](void*)
  //                              {
  //                                btn_start_encode->activate();
  //                                btn_stop_encode->deactivate();
  //                              });
  //                        });
}