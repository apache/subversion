/*
 * svn_types.i :  SWIG interface file for svn_types.h
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

/* This interface file only defines types and their related information.
   There is no module associated with this interface file. */

%import apr.i

/* -----------------------------------------------------------------------
   Create macros to define OUT parameters for "type **" params.

   OUT_PARAM(type) : create an OUT param for "type **"
   OUT_PARAM_S(type, stype) : specify a type and its swig type
*/

#define OUT_PARAM(type) \
	%typemap(ignore) type ** (type *temp) { $target = &temp; } \
	%typemap(python,argout) type ** { \
	    $target = t_output_helper($target, SWIG_NewPointerObj(*$source, \
                           SWIGTYPE_p_##type)); }
#define OUT_PARAM_S(type, stype) \
	%typemap(ignore) type ** (type *temp) { $target = (stype **)&temp; } \
	%typemap(python,argout) type ** { \
	    $target = t_output_helper($target, SWIG_NewPointerObj(*$source, \
                           SWIGTYPE_p_##stype)); }

/* -----------------------------------------------------------------------
   Define macro for forcing a type to appear in a wrapper file.
*/
#define MAKE_TYPE(type) void _ignore_##type(struct type *arg)
#define MAKE_PLAIN_TYPE(type) void _ignore_##type(type arg)

/* ----------------------------------------------------------------------- */

%typemap(python,except) svn_error_t * {
    $function
    if ($source != NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                        $source->message ? $source->message : "unknown error");
        return NULL;
    }
}
%typemap(python,out) svn_error_t * {
    /* we checked for non-NULL with the 'except' typemap, so "result" will
       always be NULL at this point. */
    Py_INCREF(Py_None);
    $target = Py_None;
}

/* ----------------------------------------------------------------------- */

/* 'svn_renum_t *' will always be an OUTPUT parameter */
%typemap(in) svn_renum_t * = long *OUTPUT;
%typemap(ignore) svn_revnum_t * = long *OUTPUT;
%typemap(argout) svn_revnum_t * = long *OUTPUT;

/* ----------------------------------------------------------------------- */

%include svn_types.h
