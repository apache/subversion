/*
 * svn_delta.i :  SWIG interface file for svn_delta.h
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
%apply SWIGTYPE **OUTPARAM {
    svn_txdelta_stream_t **,
    void **,
    svn_delta_xml_parser_t **,
    svn_txdelta_window_t **,
    svn_delta_edit_fns_t **,
    struct svn_pipe_edit_baton **,
    svn_txdelta_window_handler_t *
};

/* -----------------------------------------------------------------------
   handle the ptr/len params of svn_delta_xml_parsebytes()
*/
%typemap(python, in) (const char *buffer, apr_size_t len) {
    if (!PyString_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "expecting a string");
        return NULL;
    }
    $1 = PyString_AS_STRING($input);
    $2 = PyString_GET_SIZE($input);
}

/* -----------------------------------------------------------------------
   mark window.new_data as readonly since we would need a pool to set it
   properly (e.g. to allocate an svn_string_t structure).
*/
/* ### well, there isn't an obvious way to make it readonly, so let's
   ### just axe it altogether for now. */
%ignore svn_txdelta_window_t::new_data;
// [swig 1.3.12] %immutable svn_txdelta_window_t::new_data;

/* ----------------------------------------------------------------------- */

%include svn_delta.h
%{
#include "svn_delta.h"
%}
