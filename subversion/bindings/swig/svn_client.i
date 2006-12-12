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
 * svn_client.i: SWIG interface file for svn_client.h
 */

#if defined(SWIGPYTHON)
%module(package="libsvn") client
#elif defined(SWIGPERL)
%module "SVN::_Client"
#elif defined(SWIGRUBY)
%module "svn::ext::client"
#endif

%include svn_global.swg
%import core.i
%import svn_delta.i
%import svn_wc.i

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/

%apply Pointer NONNULL {
  const svn_opt_revision_t *revision,
  const svn_opt_revision_t *peg_revision
};

%apply const char *MAY_BE_NULL {
    const char *native_eol,
    const char *comment
};

#ifdef SWIGPYTHON
%apply svn_stream_t *WRAPPED_STREAM { svn_stream_t * };
#endif

/* -----------------------------------------------------------------------
   svn_client_proplist()
   returns apr_array_header_t * <svn_client_proplist_item_t *>
*/

/* svn_client_proplist_item_t is used exclusively for svn_client_proplist().
   The python bindings convert it to a native python tuple. */
#ifdef SWIGPYTHON
    %ignore svn_client_proplist_item_t;
#endif
#ifdef SWIGPYTHON
%typemap(argout) apr_array_header_t **props {
    svn_client_proplist_item_t **ppitem;
    int i;
    int nelts = (*$1)->nelts;
    PyObject *list = PyList_New(nelts);
    if (list == NULL)
        SWIG_fail;
    ppitem = (svn_client_proplist_item_t **)(*$1)->elts;
    for (i = 0; i < nelts; ++i, ++ppitem) {
        PyObject *item = PyTuple_New(2);
        PyObject *name = PyString_FromStringAndSize((*ppitem)->node_name->data,
                                                    (*ppitem)->node_name->len);
        PyObject *hash = svn_swig_py_prophash_to_dict((*ppitem)->prop_hash);

        if (item == NULL || name == NULL || hash == NULL) {
            Py_XDECREF(item);
            Py_XDECREF(name);
            Py_XDECREF(hash);
            Py_DECREF(list);
            SWIG_fail;
        }
        PyTuple_SET_ITEM(item, 0, name);
        PyTuple_SET_ITEM(item, 1, hash);

        PyList_SET_ITEM(list, i, item);
    }
    %append_output(list);
}
#endif

#ifdef SWIGRUBY
%typemap(argout) apr_array_header_t **props {
  %append_output(svn_swig_rb_apr_array_to_array_proplist_item(*$1));
}

%typemap(out) apr_hash_t *prop_hash
{
  $result = svn_swig_rb_prop_hash_to_hash($1);
}
#endif

#ifdef SWIGPERL
%typemap(argout) apr_array_header_t **props {
  %append_output(svn_swig_pl_convert_array(*$1,
                    $descriptor(svn_client_proplist_item_t *)));
}

%typemap(out) apr_hash_t *prop_hash {
    $result = svn_swig_pl_prophash_to_hash($1);
    argvi++;
}
#endif

#ifdef SWIGPYTHON
%callback_typemap(svn_client_get_commit_log_t log_msg_func,
                  void *log_msg_baton,
                  svn_swig_py_get_commit_log_func,
                  ,
                  )
#endif

#ifdef SWIGRUBY
%callback_typemap(svn_client_get_commit_log2_t log_msg_func2,
                  void *log_msg_baton2,
                  ,
                  ,
                  svn_swig_rb_get_commit_log_func2)

%callback_typemap(svn_cancel_func_t cancel_func, void *cancel_baton,
                  ,
                  ,
                  svn_swig_rb_cancel_func)
#endif

%callback_typemap(svn_client_blame_receiver_t receiver, void *receiver_baton,
                  svn_swig_py_client_blame_receiver_func,
                  svn_swig_pl_blame_func,
                  svn_swig_rb_client_blame_receiver_func)

#ifdef SWIGRUBY
%callback_typemap(svn_wc_notify_func2_t notify_func2, void *notify_baton2,
                  ,
                  ,
                  svn_swig_rb_notify_func2)
#endif

#ifdef SWIGPYTHON
%callback_typemap(svn_info_receiver_t receiver, void *receiver_baton,
                  svn_swig_py_info_receiver_func,
                  ,
                  )
#endif

#ifdef SWIGRUBY
%callback_typemap(svn_client_diff_summarize_func_t summarize_func,
                  void *summarize_baton,
                  ,
                  ,
                  svn_swig_rb_client_diff_summarize_func)

%callback_typemap(svn_client_list_func_t list_func, void *baton,
                  ,
                  ,
                  svn_swig_rb_client_list_func)
#endif

/* -----------------------------------------------------------------------
   We use 'svn_wc_status_t *' in some custom code, but it isn't in the
   API anywhere. Thus, SWIG doesn't generate a typemap entry for it. by
   adding a simple declaration here, SWIG will insert a name for it.
   FIXME: This may be untrue. See svn_wc_status, etc.
*/
%types(svn_wc_status_t *);

/* We also need SWIG to wrap svn_dirent_t and svn_lock_t for us.  They
   don't appear in any API, but svn_client_ls returns a hash of pointers
   to dirents and locks. */
%types(svn_dirent_t *);
%types(svn_lock_t *);

/* FIXME: What on earth is all this CALLBACK_BATON stuff actually trying to do?
   Does Python need to do anything similar?
   Why is some of it in svn_client.i and should it apply on a wider scope?
*/

#ifdef SWIGPERL
%apply void *CALLBACK_BATON {
  void *notify_baton,
  void *log_msg_baton2,
  void *cancel_baton
}
#endif

#ifdef SWIGRUBY
/* -----------------------------------------------------------------------
   CALLBACK_BATON: Do not convert to C object from Ruby object.
*/
%typemap(in) void *CALLBACK_BATON
{
  $1 = (void *)$input;
}

%apply void *CALLBACK_BATON
{
  void *notify_baton2,
  void *log_msg_baton2,
  void *cancel_baton
}
#endif

/* -----------------------------------------------------------------------
 * Convert perl hashes back into apr_hash_t * for setting the config
 * member of the svn_client_ctx_t.   This is an ugly hack, it will
 * always allocate the new apr_hash_t out of the global current_pool
 * It would be better to make apr_hash_t's into magic variables in
 * perl that are tied to the apr_hash_t interface.  This would
 * remove the need to convert to and from perl hashs all the time.
 */
#ifdef SWIGPERL
%typemap(in) apr_hash_t *config {
  $1 = svn_swig_pl_objs_to_hash_by_name ($input, "svn_config_t *",
                                         svn_swig_pl_make_pool ((SV *)NULL));
}

%typemap(out) apr_hash_t *config {
  $result = svn_swig_pl_convert_hash($1,
    $descriptor(svn_config_t *));
  argvi++;
}
#endif

#ifdef SWIGPERL
/* FIXME: For svn_commit_info_t too? */
%typemap(argout) svn_client_commit_info_t ** {
  if ($1 == NULL) {
    %append_output(&PL_sv_undef);
  } else {
    %append_output(SWIG_NewPointerObj(*$1, $*1_descriptor, 0));
  }
}
#endif

/* -----------------------------------------------------------------------
 * wcprop_changes member of svn_client_commit_info needs to be
 * converted back and forth from an array */

#ifdef SWIGPERL
%typemap(out) apr_array_header_t *wcprop_changes {
    $result = svn_swig_pl_convert_array($1,
      $descriptor(svn_prop_t *));
    argvi++;
}
#endif

/* -----------------------------------------------------------------------
 * wrap svn_client_create_context */

#ifdef SWIGPERL
%typemap(argout) svn_client_ctx_t ** {
  (*$1)->notify_func = svn_swig_pl_notify_func;
  (*$1)->notify_baton = (void *) &PL_sv_undef;
  (*$1)->log_msg_func2 = svn_swig_pl_get_commit_log_func;
  (*$1)->log_msg_baton2 = (void *) &PL_sv_undef;
  (*$1)->cancel_func = svn_swig_pl_cancel_func;
  (*$1)->cancel_baton = (void *) &PL_sv_undef;
  %append_output(SWIG_NewPointerObj(*$1, $*1_descriptor, 0));
}
#endif

/* ----------------------------------------------------------------------- */

#ifdef SWIGRUBY
%{
#include <apu.h>
#include <apr_xlate.h>
%}
%ignore svn_client_ctx_t::config;
#endif

%include svn_client_h.swg

#ifdef SWIGPYTHON

/* provide Python with access to some thunks. */
%constant svn_cancel_func_t svn_swig_py_cancel_func;
%constant svn_client_get_commit_log2_t svn_swig_py_get_commit_log_func;
%constant svn_wc_notify_func2_t svn_swig_py_notify_func;

#endif

#ifdef SWIGRUBY
%inline %{
static VALUE
svn_client_set_log_msg_func2(svn_client_ctx_t *ctx,
                             svn_client_get_commit_log2_t log_msg_func2,
                             void *log_msg_baton2,
                             apr_pool_t *pool)
{
  ctx->log_msg_func2 = log_msg_func2;
  ctx->log_msg_baton2 = log_msg_baton2;
  return (VALUE)log_msg_baton2;
}

static VALUE
svn_client_set_notify_func2(svn_client_ctx_t *ctx,
                            svn_wc_notify_func2_t notify_func2,
                            void *notify_baton2,
                            apr_pool_t *pool)
{
  ctx->notify_func2 = notify_func2;
  ctx->notify_baton2 = notify_baton2;
  return (VALUE)notify_baton2;
}

static VALUE
svn_client_set_cancel_func(svn_client_ctx_t *ctx,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *pool)
{
  ctx->cancel_func = cancel_func;
  ctx->cancel_baton = cancel_baton;
  return (VALUE)cancel_baton;
}


static VALUE
svn_client_set_config(svn_client_ctx_t *ctx,
                      apr_hash_t *config,
                      apr_pool_t *pool)
{
  ctx->config = config;
  return Qnil;
}

static svn_error_t *
svn_client_get_config(svn_client_ctx_t *ctx,
                      apr_hash_t **cfg_hash,
                      apr_pool_t *pool)
{
  *cfg_hash = ctx->config;
  return SVN_NO_ERROR;
}
%}
#endif
