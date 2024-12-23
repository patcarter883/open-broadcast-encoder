
// 1 of 7GUIs

// 7GUIs was been created as a spin-off of the master’s
// thesis Comparison of Object-Oriented and Functional
// Programming for GUI Development by Eugen Kiss at the
// Human-Computer Interaction group of the Leibniz
// Universität Hannover in 2014.

// https://7guis.github.io/7guis/

// generated by Fast Light User Interface Designer (fluid) version 1.0401

#include "test.h"

Fl_Output *counter_widget=(Fl_Output *)0;

static void cb_Count(Fl_Button*, void*) {
  int i = counter_widget->ivalue();
  i++;
  counter_widget->value(i);
}

int main(int argc, char **argv) {
  Fl_Double_Window* w;
  { Fl_Double_Window* o = new Fl_Double_Window(194, 55, "Counter");
    w = o; (void)w;
    { counter_widget = new Fl_Output(15, 15, 80, 22);
      counter_widget->value(0);
    } // Fl_Output* counter_widget
    { Fl_Button* o = new Fl_Button(99, 15, 80, 22, "Count");
      o->callback((Fl_Callback*)cb_Count);
    } // Fl_Button* o
    o->resizable(o);
    o->end();
  } // Fl_Double_Window* o
  w->show(argc, argv);
  return Fl::run();
}
