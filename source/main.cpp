#include <iostream>
#include <memory>
#include <string>

#include <FL/Enumerations.H>
#include <FL/fl_callback_macros.H>

#include "lib.hpp"


auto app = library {};


auto main(int argc, char **argv) -> int
{
  FL_FUNCTION_CALLBACK_0(app.ui->choice_input_protocol,[]{
    switch (reinterpret_cast<uintptr_t>(app.ui->choice_input_protocol->mvalue()->user_data())) {
      case 1:
        app.input_mode = sdp;
        app.ui->sdp_options_group->show();
        app.ui->ndi_options_group->hide();
        break;

      case 2:
        app.input_mode = ndi;
        app.ui->ndi_options_group->show();
        app.ui->sdp_options_group->hide();
        break;
    }
    
  }
  );

  Fl::visual(FL_DOUBLE|FL_INDEX);
  app.ui->show(argc, argv);
  const auto result = Fl::run();
  return result;
}
