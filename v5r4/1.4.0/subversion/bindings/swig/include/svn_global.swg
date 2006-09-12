/*
 * svn_global.swg :  Global SWIG definitions for Subversion
 *
 * ====================================================================
 * Copyright (c) 2005 CollabNet.  All rights reserved.
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
 */

#pragma SWIG nowarn=124

#ifdef SWIGPYTHON

%feature("autodoc",1);

%{
static PyObject * _global_svn_swig_py_pool = NULL;
%}

/* SWIG can't understand __attribute__(x), so we make it go away */
#define __attribute__(x)

/* The SWIG $argnum variable reports the current argument number
   in the underlying C code. This is a bit counterintuitive for
   Python programmers, because the underlying C function may have
   very different argument numbers than the wrapper Python function.

   SWIG doesn't provide any good way of getting Python argument 
   numbers, so we extract the argument number using macros. This 
   isn't a perfect solution, but it does the job. */
#define $svn_argnum svn_argnum_$input

/* Pointers, references, and arrays */
%typemap (arginit) POOLINIT ( \
  PyObject *_global_svn_swig_py_pool, apr_pool_t *_global_pool,
  int _global_pool_is_application_pool) 
{
  _global_pool_is_application_pool = 1;
  svn_swig_get_application_pool(&_global_svn_swig_py_pool, &_global_pool); 
}
%apply POOLINIT { void *, SWIGTYPE *, SWIGTYPE [] };

/* Object passed as pointer */
%typemap (in) void *, SWIGTYPE *, SWIGTYPE []
{
  $1 = ($1_ltype)svn_swig_MustGetPtr($input, $descriptor, $svn_argnum,
    _global_pool_is_application_pool ? &_global_svn_swig_py_pool : NULL);
  if (PyErr_Occurred()) {
    SWIG_fail;
  }
}

/* Output arguments */
%typemap (out) SWIGTYPE *, SWIGTYPE &
    "$result = svn_swig_NewPointerObj((void*)($1), $descriptor, 
                                      _global_svn_swig_py_pool);";

/* Python format specifiers. Use Python instead of SWIG to parse 
   these basic types, because Python reports better error messages 
   (with correct argument numbers). */
%typemap (in, parse="s")
  char *, char const *, char * const, char const * const "";
%typemap (in, parse="c") char "";
%typemap (in, fragment=SWIG_As_frag(long)) long
{
  $1 = ($1_ltype)SWIG_As(long)($input);
  if (SWIG_arg_fail($svn_argnum)) {
    SWIG_fail;
  }
}
%typemap (in, fragment=SWIG_As_frag(unsigned long)) unsigned long
{
  $1 = ($1_ltype)SWIG_As(unsigned long)($input);
  if (SWIG_arg_fail($svn_argnum)) {
    SWIG_fail;
  }
}
%apply long { short, int, ssize_t, enum SWIGTYPE, svn_boolean_t,
              apr_seek_where_t, apr_fileperms_t }
%apply unsigned long { unsigned char, unsigned short, unsigned int, size_t }

/* These typemaps use apr_int64_t so that they will work on Win32 and Unix */
%typemap (in) long long
  " $1 = ($1_ltype) PyLong_AsLongLong($input); ";

%typemap (in) unsigned long long
  " $1 = ($1_ltype) PyLong_AsUnsignedLongLong($input); ";

%typemap (out) apr_time_t, apr_int64_t, long long, __int64 
  " $result = PyLong_FromLongLong((apr_int64_t)($1)); ";

%typemap (out) apr_uint64_t, unsigned long long, unsigned __int64
  " $result = PyLong_FromUnsignedLongLong((apr_uint64_t)($1)); ";

%typemap(in,numinputs=0) long long *OUTPUT (apr_int64_t temp)
    "$1 = &temp;";
%typemap(argout) long long *OUTPUT
   "$result = t_output_helper($result, PyLong_FromLongLong((*$1)));";

/* We assume here that Subversion functions have no more than
   20 fixed parameters. If you want to wrap a function that has
   more fixed parameters, you'll need to add more #define
   statements. */
%{
#define svn_argnum_obj0 1
#define svn_argnum_obj1 2
#define svn_argnum_obj2 3
#define svn_argnum_obj3 4
#define svn_argnum_obj4 5
#define svn_argnum_obj5 6
#define svn_argnum_obj6 7
#define svn_argnum_obj7 8
#define svn_argnum_obj8 9
#define svn_argnum_obj9 10
#define svn_argnum_obj10 11
#define svn_argnum_obj11 12
#define svn_argnum_obj12 13
#define svn_argnum_obj13 14
#define svn_argnum_obj14 15
#define svn_argnum_obj15 16
#define svn_argnum_obj16 17
#define svn_argnum_obj17 18
#define svn_argnum_obj18 19
#define svn_argnum_obj19 20
%}

#endif

#ifdef SWIGRUBY
%{
static VALUE _global_svn_swig_rb_pool = Qnil;
%}
#endif

/* SWIG doesn't handle variadic parameters well */
%ignore svn_string_createv;
%ignore svn_stringbuf_createv;

%{
#include "svn_time.h"
#include "svn_pools.h"
%}
