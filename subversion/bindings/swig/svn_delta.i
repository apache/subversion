/*
 * svn_delta.i :  SWIG interface file for svn_delta.h
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
    svn_txdelta_window_t **,
    const svn_delta_editor_t **,
    svn_txdelta_window_handler_t *
};

/* -----------------------------------------------------------------------
   mark window.new_data as readonly since we would need a pool to set it
   properly (e.g. to allocate an svn_string_t structure).
*/
/* ### well, there isn't an obvious way to make it readonly, so let's
   ### just axe it altogether for now. */
%ignore svn_txdelta_window_t::new_data;
// [swig 1.3.12] %immutable svn_txdelta_window_t::new_data;

/* -----------------------------------------------------------------------
   thunk editors for the various language bindings.
*/

#ifdef SWIGPYTHON
void svn_swig_py_make_editor(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             PyObject *py_editor,
                             apr_pool_t *pool);
#endif

/* ----------------------------------------------------------------------- */

%include svn_delta.h
%{
#include "svn_delta.h"

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGJAVA
#include "swigutil_java.h"
#endif
%}
