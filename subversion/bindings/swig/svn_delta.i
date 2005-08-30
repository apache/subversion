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
  ID rb_id_handler = rb_intern("@handler");
  ID rb_id_handler_baton = rb_intern("@handler_baton");
  ID rb_id_pool = rb_intern("__pool__");
  VALUE obj;
  VALUE rb_handler_pool;
  apr_pool_t *handler_pool;
  svn_txdelta_window_handler_t *handler;
  void **handler_baton;

  svn_swig_rb_get_pool(0, NULL, obj, &rb_handler_pool, &handler_pool);
  handler = apr_palloc(handler_pool, sizeof(svn_txdelta_window_handler_t));
  handler_baton = apr_palloc(handler_pool, sizeof(void *));

  svn_txdelta_to_svndiff(output, pool, handler, handler_baton);

  obj = rb_class_new_instance(0, NULL, rb_cObject);
  
  rb_ivar_set(obj, rb_id_handler,
              SWIG_NewPointerObj((void *)*handler,
                                 SWIG_TypeQuery("svn_txdelta_window_handler_t"),
                                 0));
  rb_ivar_set(obj, rb_id_handler_baton,
              SWIG_NewPointerObj((void *)*handler_baton,
                                 SWIG_TypeQuery("void *"),
                                 0));
  rb_ivar_set(obj, rb_id_pool, rb_handler_pool);
  
  return obj;
}

static svn_error_t *
svn_txdelta_invoke_handler(VALUE obj, svn_txdelta_window_t *window)
{
  ID rb_id_handler = rb_intern("@handler");
  ID rb_id_handler_baton = rb_intern("@handler_baton");
  svn_txdelta_window_handler_t handler;
  void *handler_baton;

  SWIG_ConvertPtr(rb_ivar_get(obj, rb_id_handler),
                  (void **)&handler,
                  SWIG_TypeQuery("svn_txdelta_window_handler_t"),
                  1);
  SWIG_ConvertPtr(rb_ivar_get(obj, rb_id_handler_baton),
                  (void **)&handler_baton, SWIG_TypeQuery("void *"), 1);

  SVN_ERR(handler(window, handler_baton));
}
 
%}
#endif


/* -----------------------------------------------------------------------
   editor callback invokers
*/

/* Cancel the typemap as they aren't returned valued in member functions
   if editor. */
%typemap(perl5, in) (const svn_delta_editor_t *editor, void *edit_baton);

