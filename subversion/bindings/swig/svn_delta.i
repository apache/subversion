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
    const char *error_info
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


/* -----------------------------------------------------------------------
   handle svn_txdelta_window_handler_t/baton pair.
*/

%typemap(ruby, in) (svn_txdelta_window_handler_t handler,
                    void *handler_baton)
{
  $1 = svn_swig_rb_txdelta_window_handler;
  $2 = (void *)$input;
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
  $2 = (void *)$input;
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

%include svn_delta_h.swg

/* -----------------------------------------------------------------------
   handle svn_txdelta_to_svndiff().
*/
#ifdef SWIGRUBY
%inline %{
static VALUE
svn_txdelta_to_svndiff_handler(svn_stream_t *output, apr_pool_t *pool)
{
  VALUE obj;
  VALUE rb_handler_pool;
  apr_pool_t *handler_pool;
  svn_txdelta_window_handler_t *handler;
  void **handler_baton;

  obj = svn_swig_rb_make_txdelta_window_handler_wrapper(&rb_handler_pool,
                                                        &handler_pool,
                                                        &handler,
                                                        &handler_baton);
  svn_txdelta_to_svndiff(output, pool, handler, handler_baton);
  svn_swig_rb_setup_txdelta_window_handler_wrapper(obj,
                                                   *handler,
                                                   *handler_baton);
  return obj;
}

static VALUE
svn_txdelta_apply_wrapper(svn_stream_t *source,
                          svn_stream_t *target,
                          unsigned char *result_digest,
                          const char *error_info,
                          apr_pool_t *pool)
{
  VALUE obj;
  VALUE rb_handler_pool;
  apr_pool_t *handler_pool;
  svn_txdelta_window_handler_t *handler;
  void **handler_baton;

  obj = svn_swig_rb_make_txdelta_window_handler_wrapper(&rb_handler_pool,
                                                        &handler_pool,
                                                        &handler,
                                                        &handler_baton);
  svn_txdelta_apply(source, target, result_digest, error_info, pool,
                    handler, handler_baton);
  svn_swig_rb_setup_txdelta_window_handler_wrapper(obj,
                                                   *handler,
                                                   *handler_baton);
  return obj;
}

static svn_error_t *
svn_txdelta_invoke_handler(VALUE obj, svn_txdelta_window_t *window)
{
  return svn_swig_rb_invoke_txdelta_window_handler_wrapper(obj, window);
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


/* -----------------------------------------------------------------------
   editor callback invokers
*/

/* Cancel the typemap as they aren't returned valued in member functions
   if editor. */
%typemap(perl5, in) (const svn_delta_editor_t *editor, void *edit_baton);

