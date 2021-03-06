%module Systems

%{
#include "Pluto/Systems/RenderSystem/RenderSystem.hxx"
#include "Pluto/Systems/EventSystem/EventSystem.hxx"
using namespace Systems;
%}

%include "exception.i"

%exception {
  try {
    $action
  } catch (const std::exception& e) {
    SWIG_exception(SWIG_RuntimeError, e.what());
  }
}

%include "./../Tools/Typemaps.i"

%feature("autodoc", "2");

%include <windows.i>

%nodefaultctor System;
%nodefaultdtor System;

%nodefaultctor RenderSystem;
%nodefaultdtor RenderSystem;

%nodefaultctor EventSystem;
%nodefaultdtor EventSystem;

%include "Pluto/Tools/Singleton.hxx"
%include "Pluto/Tools/System.hxx"
%include "Pluto/Systems/RenderSystem/RenderSystem.hxx"
%include "Pluto/Systems/EventSystem/EventSystem.hxx"
