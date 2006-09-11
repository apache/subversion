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
 * svn_delta.i: SWIG interface file for svn_delta.h
 */

#if defined(SWIGPYTHON)
%module(package="libsvn") delta
#elif defined(SWIGPERL)
%module "SVN::_Delta"
#elif defined(SWIGRUBY)
%module "svn::ext::delta"
#endif

%include svn_global.swg
%import core.i

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/

%apply const char *MAY_BE_NULL {
    const char *error_info,
    const char *copyfrom_path,
    const char *copy_path,
    const char *base_checksum,
    const char *text_checksum
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

#ifdef SWIGPERL
%typemap(in) (const svn_delta_editor_t *EDITOR, void *BATON) {
    svn_delta_make_editor(&$1, &$2, $input, _global_pool);
}
#endif

#ifdef SWIGRUBY
%typemap(in) (const svn_delta_editor_t *EDITOR, void *BATON)
{
  if (RTEST(rb_obj_is_kind_of($input,
                              svn_swig_rb_svn_delta_editor()))) {
    $1 = svn_swig_rb_to_swig_type($input,
                                  "svn_delta_editor_t *",
                                  _global_pool);
    $2 = svn_swig_rb_to_swig_type(rb_funcall($input, rb_intern("baton"), 0),
                                  "void *", _global_pool);
  } else {
    svn_swig_rb_make_delta_editor(&$1, &$2, $input, _global_pool);
  }
}
#endif

#ifndef SWIGPYTHON
/* Python users have to use svn_swig_py_make_editor manually, which sucks.
   Maybe we could allow people to pass a python object in the editor parameter,
   and None as the baton, and automatically invoke svn_swig_py_make_editor,
   rather than forcing the svn_swig_py_make_editor to be done manually.
   Of course, ideally, the baton parameter would vanish from the python
   side entirely, but we can't kill compatibility like that until 2.0.
*/
%apply (const svn_delta_editor_t *EDITOR, void *BATON)
{
  (const svn_delta_editor_t *editor, void *baton),
  (const svn_delta_editor_t *editor, void *edit_baton),
  (const svn_delta_editor_t *editor, void *file_baton),
  (const svn_delta_editor_t *diff_editor, void *diff_baton),
  (const svn_delta_editor_t *update_editor, void *update_baton)
}
#endif

/* -----------------------------------------------------------------------
   handle svn_txdelta_window_handler_t/baton pair.
*/

#ifdef SWIGRUBY
%typemap(in) (svn_txdelta_window_handler_t handler,
                    void *handler_baton)
{
  if (RTEST(rb_obj_is_kind_of($input,
                              svn_swig_rb_svn_delta_text_delta_window_handler()))) {
    $1 = svn_swig_rb_to_swig_type($input,
                                  "svn_txdelta_window_handler_t",
                                  _global_pool);
    $2 = svn_swig_rb_to_swig_type(rb_funcall($input, rb_intern("baton"), 0),
                                  "void *", _global_pool);
  } else {
    $1 = svn_swig_rb_txdelta_window_handler;
    $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
  }
}
#endif

/* -----------------------------------------------------------------------
   handle svn_delta_path_driver().
*/

#ifdef SWIGRUBY
%callback_typemap(svn_delta_path_driver_cb_func_t callback_func,
                  void *callback_baton,
                  ,
                  ,
                  svn_swig_rb_delta_path_driver_cb_func)
#endif

/* ----------------------------------------------------------------------- */

%{
#include "svn_md5.h"
%}

/* -----------------------------------------------------------------------
   handle svn_txdelta_window_t::ops
*/
#ifdef SWIGRUBY
%ignore svn_txdelta_window_t::ops;
%inline %{
static VALUE
svn_txdelta_window_t_ops_get(svn_txdelta_window_t *window)
{
  return svn_swig_rb_txdelta_window_t_ops_get(window);
}
%}
#endif


%include svn_delta_h.swg

/* -----------------------------------------------------------------------
   handle svn_txdelta_apply_instructions()
*/
#ifdef SWIGRUBY
%inline %{
static VALUE
svn_swig_rb_txdelta_apply_instructions(svn_txdelta_window_t *window,
                                       const char *sbuf)
{
  char *tbuf;
  apr_size_t tlen;

  tlen = window->tview_len + 1;
  tbuf = ALLOCA_N(char, tlen);
  svn_txdelta_apply_instructions(window, sbuf, tbuf, &tlen);

  return rb_str_new(tbuf, tlen);
}
%}
#endif

/* -----------------------------------------------------------------------
   handle svn_txdelta_to_svndiff().
*/
#ifdef SWIGRUBY
%inline %{
static void
svn_txdelta_apply_wrapper(svn_stream_t *source,
                          svn_stream_t *target,
                          unsigned char *result_digest,
                          const char *error_info,
                          svn_txdelta_window_handler_t *handler,
                          void **handler_baton,
                          apr_pool_t *pool)
{
  svn_txdelta_apply(source, target, result_digest, error_info,
                    pool, handler, handler_baton);
}

static svn_error_t *
svn_txdelta_invoke_window_handler_wrapper(VALUE obj,
                                          svn_txdelta_window_t *window,
                                          apr_pool_t *pool)
{
  return svn_swig_rb_invoke_txdelta_window_handler_wrapper(obj, window, pool);
}

static const char *
svn_txdelta_md5_digest_as_cstring(svn_txdelta_stream_t *stream,
                                  apr_pool_t *pool)
{
  const unsigned char *digest;

  digest = svn_txdelta_md5_digest(stream);

  if (digest) {
    return svn_md5_digest_to_cstring(digest, pool);
  } else {
    return NULL;
  }
}

%}
#endif
