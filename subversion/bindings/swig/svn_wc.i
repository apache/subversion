/*
 * svn_wc.i :  SWIG interface file for svn_wc.h
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

%module wc
%include typemaps.i

%import apr.i
%import svn_types.i
%import svn_string.i
%import svn_delta.i

/* -----------------------------------------------------------------------
   ### these functions require a pool, which we don't have immediately
   ### handy. just eliminate these funcs for now.
*/
%ignore svn_wc_set_auth_file;

/* ### ignore this structure because the accessors will need a pool */
%ignore svn_wc_keywords_t;

/* -----------------------------------------------------------------------
   these types (as 'type **') will always be an OUT param
*/
%apply SWIGTYPE **OUTPARAM {
    svn_wc_entry_t **,
    svn_wc_adm_access_t **,
    svn_wc_status_t **
};

/* we can't use the OUTPARAM cuz that is only for pointers. use the
   standard OUTPARAM definition for 'int' instead. */
%apply int *OUTPUT { svn_boolean_t * };
%apply int *OUTPUT { int * };

/* handle the property hash returned by svn_wc_prop_list */
%apply apr_hash_t **PROPHASH { apr_hash_t **props };

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
   handle svn_cancel_func_t/baton pairs
*/

%typemap(python,in) (svn_cancel_func_t cancel_func, void *cancel_baton) {

  $1 = svn_swig_py_cancel_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(java,in) (svn_cancel_func_t cancel_func, void *cancel_baton) {

  $1 = svn_swig_java_cancel_func;
  $2 = (void*)$input; /* our function is the baton. */
}

%typemap(jni) svn_cancel_func_t "jobject"
%typemap(jtype) svn_cancel_func_t "org.tigris.subversion.Canceller"
%typemap(jstype) svn_cancel_func_t "org.tigris.subversion.Canceller"
%typemap(javain) svn_cancel_func_t "$javainput"
%typemap(javaout) svn_cancel_func_t {
    return $jnicall;
  }

/* ----------------------------------------------------------------------- */

%include svn_wc.h
%{
#include "svn_wc.h"

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGJAVA
#include "swigutil_java.h"
#endif
%}
