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

%include typemaps.i

%include svn_global.swg
%import apr.swg
%import core.i
%import svn_types.swg
%import svn_string.swg

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

%apply const char *MAY_BE_NULL {
    const char *error_info,
    const char *copyfrom_path,
    const char *copy_path,
    const char *base_checksum
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

%typemap(perl5, in) (const svn_delta_editor_t *EDITOR, void *BATON) {
    svn_delta_make_editor(&$1, &$2, $input, _global_pool);
}

%typemap(ruby, in) (const svn_delta_editor_t *EDITOR, void *BATON)
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

%apply (const svn_delta_editor_t *EDITOR, void *BATON)
{
  (const svn_delta_editor_t *editor, void *edit_baton),
  (const svn_delta_editor_t *update_editor, void *update_baton),
  (const svn_delta_editor_t *switch_editor, void *switch_baton),
  (const svn_delta_editor_t *status_editor, void *status_baton)
}

/* -----------------------------------------------------------------------
   handle svn_txdelta_window_handler_t/baton pair.
*/

%typemap(ruby, in) (svn_txdelta_window_handler_t handler,
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

/* -----------------------------------------------------------------------
   handle svn_delta_path_driver().
*/

%typemap(ruby, in) apr_array_header_t *paths
{
  $1 = svn_swig_rb_strings_to_apr_array($input, _global_pool);
}

%typemap(ruby, in) (svn_delta_path_driver_cb_func_t callback_func,
                    void *callback_baton)
{
  $1 = svn_swig_rb_delta_path_driver_cb_func;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

/* ----------------------------------------------------------------------- */

%{
#include "svn_md5.h"

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
 
/* FIXME: Is this even required any more, now that the arguments have
   been re-ordered so that pool is last? */
static void
svn_txdelta_to_svndiff2_wrapper(svn_stream_t *output,
                                svn_txdelta_window_handler_t *handler,
                                void **handler_baton,
                                int svndiff_version,
                                apr_pool_t *pool)
{
  svn_txdelta_to_svndiff2(handler, handler_baton, output, svndiff_version,
                          pool);
}
 
static svn_error_t *
svn_txdelta_invoke_window_handler(VALUE window_handler,
                                  svn_txdelta_window_t *window,
                                  apr_pool_t *pool)
{
  return svn_swig_rb_invoke_txdelta_window_handler(window_handler,
                                                   window, pool);
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
