#ifdef SWIGMZSCHEME
#ifndef SWIG_fail
#define SWIG_fail SWIG_contract_assert(0,"runtime failure")
#endif
%define %append_output(obj) SWIG_APPEND_VALUE(obj) %enddef
#endif