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

#if defined(SWIGPERL)
%module "SVN::_Delta"
#elif defined(SWIGRUBY)
%module "svn::ext::delta"
#else
%module delta
#endif

%include "typemaps.i"

%import apr.i
%import svn_types.i
%import svn_string.i

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/
%apply SWIGTYPE **OUTPARAM {
    svn_txdelta_stream_t **,
    void **,
    svn_txdelta_window_t **,
    const svn_delta_editor_t **,
    svn_txdelta_window_handler_t *
};

#ifdef SWIGPYTHON
%apply svn_stream_t *WRAPPED_STREAM { svn_stream_t * };
#endif

/* -----------------------------------------------------------------------
   mark window.new_data as readonly since we would need a pool to set it
   properly (e.g. to allocate an svn_string_t structure).
*/
%immutable svn_txdelta_window_t::new_data;

/* -----------------------------------------------------------------------
   thunk editors for the various language bindings.
*/

#ifdef SWIGPYTHON
void svn_swig_py_make_editor(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             PyObject *py_editor,
                             apr_pool_t *pool);
#endif

%typemap(perl5, in) (const svn_delta_editor_t *editor, void *edit_baton) {
    svn_delta_make_editor(&$1, &$2, $input, _global_pool);
}

#ifdef SWIGRUBY
void svn_swig_rb_make_editor(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             VALUE rb_editor,
                             apr_pool_t *pool);
#endif

/* ----------------------------------------------------------------------- */

%{
#include "svn_md5.h"
#include "svn_delta.h"

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGPERL
#include "swigutil_pl.h"
#endif

#ifdef SWIGRUBY
#include "swigutil_rb.h"
#endif
%}

%include svn_delta.h

/* -----------------------------------------------------------------------
   editor callback invokers
*/

/* Cancel the typemap as they aren't returned valued in member functions
   if editor. */
%typemap(perl5, in) (const svn_delta_editor_t *editor, void *edit_baton);

#ifdef SWIGPERL
%include delta_editor.hi
#endif

#ifdef SWIGRUBY
REMOVE_DESTRUCTOR(svn_txdelta_op_t)
REMOVE_DESTRUCTOR(svn_txdelta_window_t)
REMOVE_DESTRUCTOR(svn_delta_editor_t)
#endif
