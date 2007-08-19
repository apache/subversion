#ifdef SWIGMZSCHEME
#ifndef SWIG_fail
#define SWIG_fail SWIG_contract_assert(0,"runtime failure")
#endif
%define %append_output(obj) SWIG_APPEND_VALUE(obj) %enddef
%{
#include "./mzscheme/libsvn_swig_mzscheme/mzscheme.h"
#define NIL_P(arg) (arg==0)
#define _swig_convert_struct__apr_uint32_t(a) *a
#define _swig_convert_struct__apr_uint64_t(a) *a
#define _swig_convert_struct__apr_int32_t(a) *a
#define _swig_convert_struct__apr_int64_t(a) *a
#define _swig_convert_struct__apr_size_t(a) *a
#define _swig_convert_struct__apr_byte_t(a) *a
#define _swig_convert_struct__apr_off_t(a) *a
#ifndef MZSCHEME_VERSION
#define MZSCHEME_VERSION "352"
#endif
#if 0
Scheme_Object *scheme_initialize(Scheme_Env *env)
{
  return scheme_make_string("hobo");
}
Scheme_Object *scheme_reload(Scheme_Env *env)
{
	return scheme_initialize(env);	// Nothing special for reload
}
#endif
char *scheme_initialize_internal()
{
  printf("initilize internal\n");
//  return scheme_initialize(env); // Magic!!
return MZSCHEME_VERSION;
}

%}
#endif