/*
 * svn_client.i :  SWIG interface file for svn_client.h
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

#ifdef SWIGPERL
%module "SVN::_Client"
#else
%module client
#endif

%include typemaps.i

%import apr.i
%import svn_types.i
%import svn_string.i
%import svn_delta.i

/* -----------------------------------------------------------------------
   don't wrap the following items
*/
#ifndef SWIGPERL
    %ignore svn_client_proplist_item_t;
#endif

/* -----------------------------------------------------------------------
   these types (as 'type **') will always be an OUT param
*/
%apply SWIGTYPE **OUTPARAM {
  svn_client_commit_info_t **,
  svn_auth_provider_object_t **,
  svn_client_ctx_t **
};

/* -----------------------------------------------------------------------
   all "targets" and "diff_options" arrays are constant inputs of
   svn_stringbuf_t *
 */
%apply const apr_array_header_t *STRINGLIST {
    const apr_array_header_t *targets,
    const apr_array_header_t *diff_options
};

/* -----------------------------------------------------------------------
   fix up the return hash for svn_client_propget()
*/
%apply apr_hash_t **PROPHASH { apr_hash_t **props };

/* -----------------------------------------------------------------------
   handle the return value for svn_client_proplist()
*/

%typemap(python,in,numinputs=0) apr_array_header_t **props (apr_array_header_t *temp) {
    $1 = &temp;
}
%typemap(python,argout,fragment="t_output_helper") apr_array_header_t **props {
    svn_client_proplist_item_t **ppitem;
    int i;
    int nelts = (*$1)->nelts;
    PyObject *list = PyList_New(nelts);
    if (list == NULL)
        return NULL;
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
            return NULL;
        }
        PyTuple_SET_ITEM(item, 0, name);
        PyTuple_SET_ITEM(item, 1, hash);

        PyList_SET_ITEM(list, i, item);
    }
    $result = t_output_helper($result, list);
}

%typemap(perl5,in,numinputs=0) apr_array_header_t **props (apr_array_header_t *temp)
{
    $1 = &temp;
}

%typemap(perl5,argout) apr_array_header_t **props {
    $result = svn_swig_pl_convert_array(*$1,SWIG_TypeQuery(
                                              "svn_client_proplist_item_t *"));
    argvi++;
}

%typemap(perl5,out) apr_hash_t *prop_hash {
    $result = svn_swig_pl_prophash_to_hash($1);
    argvi++;
}

/* -----------------------------------------------------------------------
   handle svn_wc_notify_func_t/baton pairs
*/

%typemap(python,in) (svn_wc_notify_func_t notify_func, void *notify_baton) {

  $1 = svn_swig_py_notify_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(java,in) (svn_wc_notify_func_t notify_func, void *notify_baton) {

  $1 = svn_swig_java_notify_func;
  $2 = (void*)$input; /* our function is the baton. */
}

%typemap(jni) svn_wc_notify_func_t "jobject"
%typemap(jtype) svn_wc_notify_func_t "org.tigris.subversion.wc.Notifier"
%typemap(jstype) svn_wc_notify_func_t "org.tigris.subversion.wc.Notifier"
%typemap(javain) svn_wc_notify_func_t "$javainput"
%typemap(javaout) svn_wc_notify_func_t {
    return $jnicall;
  }

/* -----------------------------------------------------------------------
   handle svn_wc_notify_func_t/baton pairs
*/

%typemap(python,in) (svn_wc_status_func_t status_func, void *status_baton) {
  $1 = svn_swig_py_status_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(perl5,in) (svn_wc_status_func_t status_func, void *status_baton) {
  $1 = svn_swig_pl_status_func;
  $2 = $input; /* our function is the baton. */
}

/* -----------------------------------------------------------------------
   handle svn_client_get_commit_log_t/baton pairs
*/

%typemap(python,in) (svn_client_get_commit_log_t log_msg_func, 
                     void *log_msg_baton) {

  $1 = svn_swig_py_get_commit_log_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(java,in) (svn_client_get_commit_log_t log_msg_func, 
                   void *log_msg_baton) {

  $1 = svn_swig_java_get_commit_log_func;
  $2 = (void*)$input; /* our function is the baton. */
}

%typemap(jni) svn_client_get_commit_log_t "jobject"
%typemap(jtype) svn_client_get_commit_log_t "org.tigris.subversion.client.ClientPrompt"
%typemap(jstype) svn_client_get_commit_log_t "org.tigris.subversion.client.ClientPrompt"
%typemap(javain) svn_client_get_commit_log_t "$javainput"
%typemap(javaout) svn_client_get_commit_log_t {
    return $jnicall;
  }

/* -----------------------------------------------------------------------
   handle svn_client_prompt_t/baton pairs
*/

%typemap(java,memberin) (svn_client_prompt_t prompt_func, 
                   void *prompt_baton) {
  //$1 = svn_swig_java_client_prompt_func;
  //$2 = svn_swig_java_make_callback_baton(jenv, $input, _global_pool);
}

%typemap(java,in) (svn_client_prompt_t prompt_func, 
                   void *prompt_baton) {
  $1 = svn_swig_java_client_prompt_func;
  $2 = svn_swig_java_make_callback_baton(jenv, $input, _global_pool);
}

%typemap(java, jni) svn_client_prompt_t "jobject"
%typemap(java, jtype) svn_client_prompt_t "org.tigris.subversion.client.ClientPrompt"
%typemap(java, jstype) svn_client_prompt_t "org.tigris.subversion.client.ClientPrompt"
%typemap(java, javain) svn_client_prompt_t "$javainput"

/* -----------------------------------------------------------------------
   handle svn_log_message_receiver_t/baton pairs
*/

%typemap(java,in) (svn_log_message_receiver_t receiver,
                void *receiver_baton) {

  $1 = svn_swig_java_log_message_receiver;
  $2 = (void*)$input; /* our function is the baton. */
}

%typemap(jni) svn_log_message_receiver_t "jobject"
%typemap(jtype) svn_log_message_receiver_t "org.tigris.subversion.client.LogMessageReceiver"
%typemap(jstype) svn_log_message_receiver_t "org.tigris.subversion.client.LogMessageReceiver"
%typemap(javain) svn_log_message_receiver_t "$javainput"
%typemap(javaout) svn_log_message_receiver_t {
    return $jnicall;
  }

/* -----------------------------------------------------------------------
   handle svn_client_blame_receiver_t/baton pairs
*/

%typemap(python, in) (svn_client_blame_receiver_t receiver, 
                      void *receiver_baton) {
    $1 = svn_swig_py_client_blame_receiver_func;
    $2 = (void *)$input;
}

%typemap(perl5,in) (svn_client_blame_receiver_t receiver, void *receiver_baton) {
  $1 = svn_swig_pl_blame_func;
  $2 = $input; /* our function is the baton. */
}

/* -----------------------------------------------------------------------
   handle the "statushash" OUTPUT param for svn_client_status()
*/
%typemap(python,in,numinputs=0) apr_hash_t **statushash = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **statushash {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, SWIGTYPE_p_svn_wc_status_t));
}

/* -----------------------------------------------------------------------
   handle the prompt_baton
*/

%typemap(jni) svn_log_message_receiver_t "jobject"
%typemap(jtype) svn_log_message_receiver_t "org.tigris.subversion.client.LogMessageReceiver"
%typemap(jstype) svn_log_message_receiver_t "org.tigris.subversion.client.LogMessageReceiver"
%typemap(javain) svn_log_message_receiver_t "$javainput"
%typemap(javaout) svn_log_message_receiver_t {
    return $jnicall;
  }

/* -----------------------------------------------------------------------
   We use 'svn_wc_status_t *' in some custom code, but it isn't in the
   API anywhere. Thus, SWIG doesn't generate a typemap entry for it. by
   adding a simple declaration here, SWIG will insert a name for it.
*/
%types(svn_wc_status_t *);

/* We also need SWIG to wrap svn_dirent_t for us.  It doesn't appear in
   any API, but svn_client_ls returns a hash of pointers to dirents. */
%types(svn_dirent_t *);

/* -----------------------------------------------------------------------
  thunk the various authentication prompt functions.
  PERL NOTE: store the inputed SV in _global_callback for use in the
             later argout typemap
*/
%typemap(perl5, in) (svn_auth_simple_prompt_func_t prompt_func,
                     void *prompt_baton) {
    $1 = svn_swig_pl_thunk_simple_prompt;
    _global_callback = $input;
    $2 = (void *) _global_callback;
}
%typemap(python, in) (svn_auth_simple_prompt_func_t prompt_func,
                      void *prompt_baton) {
    $1 = svn_swig_py_auth_simple_prompt_func;
    $2 = $input;
}

%typemap(perl5, in) (svn_auth_username_prompt_func_t prompt_func,
                     void *prompt_baton) {
    $1 = svn_swig_pl_thunk_username_prompt;
    _global_callback = $input;
    $2 = (void *) _global_callback;
}
%typemap(python, in) (svn_auth_username_prompt_func_t prompt_func,
                      void *prompt_baton) {
    $1 = svn_swig_py_auth_username_prompt_func;
    $2 = $input;
}

%typemap(perl5, in) (svn_auth_ssl_server_trust_prompt_func_t prompt_func,
                     void *prompt_baton) {
    $1 = svn_swig_pl_thunk_ssl_server_trust_prompt;
    _global_callback = $input;
    $2 = (void *) _global_callback;
}
%typemap(python, in) (svn_auth_ssl_server_trust_prompt_func_t prompt_func,
                     void *prompt_baton) {
    $1 = svn_swig_py_auth_ssl_server_trust_prompt_func;
    $2 = $input;
}

%typemap(perl5, in) (svn_auth_ssl_client_cert_prompt_func_t prompt_func,
                      void *prompt_baton) {
    $1 = svn_swig_pl_thunk_ssl_client_cert_prompt;
    _global_callback = $input;
    $2 = (void *) _global_callback;
}
%typemap(python, in) (svn_auth_ssl_client_cert_prompt_func_t prompt_func,
                      void *prompt_baton) {
    $1 = svn_swig_py_auth_ssl_client_cert_prompt_func;
    $2 = $input;
}

%typemap(perl5, in) (svn_auth_ssl_client_cert_pw_prompt_func_t prompt_func,
                      void *prompt_baton) {
    $1 = svn_swig_pl_thunk_ssl_client_cert_pw_prompt;
    _global_callback = $input;
    $2 = (void *) _global_callback;
}
%typemap(python, in) (svn_auth_ssl_client_cert_pw_prompt_func_t prompt_func,
                      void *prompt_baton) {
    $1 = svn_swig_py_auth_ssl_client_cert_pw_prompt_func;
    $2 = $input;
}

/* -----------------------------------------------------------------------
 * For all the various functions that set a callback baton create a reference
 * for the baton (which in this case is an SV pointing to the callback)
 * and make that a return from the function.  The perl side should
 * then store the return in the object the baton is attached to.
 * If the function already returns a value then this value is follows that
 * function.  In the case of the prompt functions auth_open_helper in Core.pm
 * is used to split up these values.
*/
%typemap(perl5, argout) void *CALLBACK_BATON (SV * _global_callback) {
  /* callback baton */
  $result = sv_2mortal (newRV_inc (_global_callback)); argvi++; }

%typemap(perl5, in) void *CALLBACK_BATON (SV * _global_callback) {
  _global_callback = $input;
  $1 = (void *) _global_callback;
}

#ifdef SWIGPERL
%apply void *CALLBACK_BATON {
  void *prompt_baton,
  void *notify_baton,
  void *log_msg_baton,
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
%typemap(perl5, in) apr_hash_t *config {
  $1 = svn_swig_pl_objs_to_hash_by_name ($input, "svn_config_t *",
                                         svn_swig_pl_make_pool ((SV *)NULL));
}

%typemap(perl5, out) apr_hash_t *config {
  $result = svn_swig_pl_convert_hash($1, SWIG_TypeQuery("svn_config_t *"));
  argvi++;
}

/* -----------------------------------------------------------------------
 * override default typemap for svn_client_commit_info_t for perl.  Some calls
 * never allocate and fill the commit_info struct.  This lets us return
 * undef for them.  Otherwise the object we pass back can cause crashes */
%typemap(perl5, in, numinputs=0) svn_client_commit_info_t ** 
                                 ( svn_client_commit_info_t * temp ) {
    temp = NULL;
    $1 = &temp;
}

%typemap(perl5, argout) svn_client_commit_info_t ** {
    if ($1 == NULL) {
        $result = &PL_sv_undef;
        argvi++;
    }  else {
        $result = sv_newmortal();
        SWIG_MakePtr($result, (void *)*$1,
                     $*1_descriptor, 0);
        argvi++;
    }
}

/* -----------------------------------------------------------------------
 * wcprop_changes member of svn_client_commit_info needs to be
 * converted back and forth from an array */

%typemap(perl5, out) apr_array_header_t *wcprop_changes {
    $result = svn_swig_pl_convert_array($1,SWIG_TypeQuery("svn_prop_t *"));
    argvi++;
}

/* -----------------------------------------------------------------------
 * wrap svn_client_create_context */

%typemap(perl5,in,numinputs=0) svn_client_ctx_t ** (svn_client_ctx_t *temp) {
    $1 = &temp;
}

%typemap(perl5,argout) svn_client_ctx_t ** {
  (*$1)->notify_func = svn_swig_pl_notify_func;
  (*$1)->notify_baton = (void *) &PL_sv_undef;
  (*$1)->log_msg_func = svn_swig_pl_get_commit_log_func;
  (*$1)->log_msg_baton = (void *) &PL_sv_undef;
  (*$1)->cancel_func = svn_swig_pl_cancel_func;
  (*$1)->cancel_baton = (void *) &PL_sv_undef;
  $result = sv_newmortal();
  SWIG_MakePtr($result, (void *)*$1,
               $*1_descriptor, 0);
  argvi++;
}  


/* -----------------------------------------------------------------------
 * Handle output types for svn_client_url_from_path, svn_client_uuid_from_url
 * and svn_client_uuid_from_path */

%apply const char **OUTPUT {
    const char **url,
    const char **uuid
};




/* ----------------------------------------------------------------------- */

%typemap(java, in) svn_stream_t *out %{
    $1 = svn_swig_java_outputstream_to_stream(jenv, $input, _global_pool);
%}
%typemap(java, jni) svn_stream_t * "jobject";
%typemap(java, jtype) svn_stream_t * "java.io.OutputStream";
%typemap(java, jstype) svn_stream_t * "java.io.OutputStream";
%typemap(java, javain) svn_stream_t * "$javainput";

/* ----------------------------------------------------------------------- */

/* Include the headers before we swig-include the svn_client.h header file.
   SWIG will split the nested svn_client_revision_t structure, and we need
   the types declared *before* the split structure is encountered.  */

%{
#include "svn_client.h"
#include "svn_time.h"

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGJAVA
#include "swigutil_java.h"
#endif

#ifdef SWIGPERL
#include "swigutil_pl.h"
#endif
%}

%include svn_client.h
