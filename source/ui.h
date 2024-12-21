// generated by Fast Light User Interface Designer (fluid) version 1.0401

#ifndef ui_h
#define ui_h
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Flex.H>
#include <FL/Fl_Choice.H>

class user_interface {
public:
  user_interface();
  Fl_Double_Window *main_window;
  Fl_Flex *pack;
  Fl_Choice *choice_input_protocol;
  static Fl_Menu_Item menu_choice_input_protocol[];
  static Fl_Menu_Item *select_sdp_input;
  static Fl_Menu_Item *select_ndi_input;
  Fl_Flex *sdp_options_group;
  Fl_Flex *ndi_options_group;
  void show(int argc, char **argv) const;
};
#endif
