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

#ifdef SWIGPYTHON

#if SVN_SWIG_VERSION >= 103024
%feature("autodoc",1);
#else
/* Old versions of SWIG can't handle __attribute__ */
#define __attribute__(x)

/* Old versions of SWIG can't handle functions
   where the pool is not the last argument */
%ignore svn_stream_printf;
%ignore svn_stream_printf_from_utf8;
%ignore svn_opt_print_generic_help;
#endif

/* Don't generate default constructors */
%nodefault;

%{
static PyObject * _global_svn_swig_py_pool = NULL;
%}

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
  PyObject *_global_svn_swig_py_pool, apr_pool_t *_global_pool) 
{
  svn_swig_get_application_pool(&_global_svn_swig_py_pool, &_global_pool); 
}
%apply POOLINIT { void *, SWIGTYPE *, SWIGTYPE [] };

/* Object passed as pointer */
%typemap (in) void *, SWIGTYPE *, SWIGTYPE []
{
  $1 = ($1_ltype)svn_swig_MustGetPtr($input, $descriptor, $svn_argnum,
    _global_svn_swig_py_is_local_pool ? NULL : &_global_svn_swig_py_pool);
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
%typemap (in, parse="B") unsigned char "";
%typemap (in, parse="h") short "";
%typemap (in, parse="H") unsigned short "";
%typemap (in, parse="i") int, enum SWIGTYPE, svn_boolean_t, apr_seek_where_t, \
  apr_fileperms_t "";
%typemap (in, parse="I") unsigned int "";
%typemap (in, parse="l") long, ssize_t "";
%typemap (in, parse="k") unsigned long, size_t "";
%typemap (in, parse="L") long long, __int64 "";
%typemap (in, parse="K") unsigned long long, unsigned __int64 "";

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

/* SWIG doesn't handle variadic parameters well */
%ignore svn_string_createv;
%ignore svn_stringbuf_createv;
