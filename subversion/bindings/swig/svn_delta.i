/*
 * svn_delta.i :  SWIG interface file for svn_delta.h
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

%module _delta

%include "typemaps.i"

%import apr.i
%import svn_types.i
%import svn_string.i

/* -----------------------------------------------------------------------
   For these types, "type **" is always an OUT param.
*/

OUT_PARAM(svn_txdelta_stream_t);
OUT_PARAM(void);
OUT_PARAM(svn_delta_xml_parser_t);
OUT_PARAM(svn_txdelta_window_t);
OUT_PARAM(svn_delta_edit_fns_t);

OUT_PARAM_S(const svn_delta_edit_fns_t, svn_delta_edit_fns_t);
OUT_PARAM_S(struct svn_pipe_edit_baton, svn_pipe_edit_baton);

/* -----------------------------------------------------------------------
   all uses of "svn_txdelta_window_handler_t *" are OUT params
*/
%typemap(ignore) svn_txdelta_window_handler_t * (svn_txdelta_window_handler_t temp) {
    $target = &temp;
}
%typemap(python,argout) svn_txdelta_window_handler_t * {
    $target = t_output_helper($target,
                              SWIG_NewPointerObj(*$source,
                                                 SWIGTYPE_svn_txdelta_window_handler_t));
}

/* -----------------------------------------------------------------------
   handle the ptr/len params of svn_delta_xml_parsebytes()
*/
%typemap(python, in) const char *buffer {
    if (!PyString_Check($source)) {
        PyErr_SetString(PyExc_TypeError, "not a string");
        return NULL;
    }
    $target = PyString_AS_STRING($source);
}

%typemap(ignore) apr_size_t len { }
%typemap(python, check) apr_size_t len {
    $target = PyString_GET_SIZE(obj0);
}

/* ----------------------------------------------------------------------- */

%include svn_delta.h
%{
#include "svn_delta.h"
%}
