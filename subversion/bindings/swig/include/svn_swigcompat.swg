/*
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 *
 * svn_swigcompat.swg: Definitions providing compatibility with old
 *   SWIG version.
 */

/* %append_output(obj) */
#if SWIG_VERSION <= 0x010327
#ifdef SWIGPYTHON
%fragment("t_output_helper");
#define %append_output(obj) $result = t_output_helper($result, obj)
#endif

#ifdef SWIGPERL
#define %append_output(obj) \
  if (argvi >= items) EXTEND(sp,1); $result = obj; argvi++
#endif

#ifdef SWIGRUBY
%fragment("output_helper");
#define %append_output(obj) $result = output_helper($result, obj)
#endif
#endif


/* %set_constant(name, value) */
#if SWIG_VERSION <= 0x010327
#ifdef SWIGPYTHON
%define %set_constant(name, value)
PyDict_SetItemString(d, name, value);
%enddef
#endif
#endif


#ifdef SWIGPERL
#if SWIG_VERSION == 0x010324
%typemap(varin) apr_pool_t * {
  void *temp;
  if (SWIG_ConvertPtr($input, (void **) &temp, $1_descriptor,0) < 0) {
    croak("Type error in argument $argnum of $symname. Expected $1_mangle");
  }
  $1 = ($1_ltype) temp;
}
#endif
#endif


#ifdef SWIGRUBY
#if SWIG_VERSION <= 0x010327
/* Remove compiler warning (break strict-aliasing rules) */
%typemap(in) SWIGTYPE * (void *temp)
{
#if SWIG_VERSION <= 0x010325
  SWIG_ConvertPtr($input, &temp, $1_descriptor, 1);
#else
  SWIG_ConvertPtr($input, &temp, $1_descriptor, $disown);
#endif
  $1 = ($1_ltype)temp;
}
#endif

#if SWIG_VERSION <= 0x010324
/* for SWIG bug */
%define OUTPUT_TYPEMAP(type, converter, convtype)
%typemap(in, numinputs=0) type *OUTPUT ($*1_ltype temp),
                          type &OUTPUT ($*1_ltype temp) "$1 = &temp;";
%typemap(argout, fragment="output_helper") type *OUTPUT, type &OUTPUT {
   VALUE o = converter(convtype (*$1));
   $result = output_helper($result, o);
}
%enddef

OUTPUT_TYPEMAP(long, INT2NUM, (long));
OUTPUT_TYPEMAP(long long, LL2NUM, (apr_int64_t));
OUTPUT_TYPEMAP(unsigned long, UINT2NUM, (unsigned long));
OUTPUT_TYPEMAP(unsigned long long, ULL2NUM, (apr_uint64_t));

#undef OUTPUT_TYPEMAP
#endif
#endif
