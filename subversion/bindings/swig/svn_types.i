/*
 * svn_types.i :  SWIG interface file for svn_types.h
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

/* This interface file only defines types and their related information.
   There is no module associated with this interface file. */

%import apr.i

/* -----------------------------------------------------------------------
   Create a typemap to define "type **" as OUT parameters.

   Note: SWIGTYPE is just a placeholder for "some arbitrary type". This
         typemap will be applied onto a "real" type.
*/

%typemap(python, in, numinputs=0) SWIGTYPE **OUTPARAM ($*1_type temp) {
    $1 = ($1_ltype)&temp;
}
%typemap(java, in) SWIGTYPE **OUTPARAM ($*1_type temp) {
    $1 = ($1_ltype)&temp;
}
%typemap(perl5, in, numinputs=0) SWIGTYPE **OUTPARAM ($*1_type temp) {
    $1 = ($1_ltype)&temp;
}
%typemap(python, argout, fragment="t_output_helper") SWIGTYPE **OUTPARAM {
    $result = t_output_helper($result,
                              SWIG_NewPointerObj(*$1, $*1_descriptor, 0));
}
%typemap(perl5, argout) SWIGTYPE **OUTPARAM {
    ST(argvi) = sv_newmortal();
    SWIG_MakePtr(ST(argvi++), (void *)*$1, $*1_descriptor,0);
}

/* -----------------------------------------------------------------------
   Create a typemap to handle enums.
*/

%typemap(python, in, numinputs=0) enum SWIGTYPE *OUTENUM ($*1_type temp) {
    $1 = ($1_ltype)&temp;
}
%typemap(perl5, in, numinputs=0) enum SWIGTYPE *OUTENUM ($*1_type temp) {
    $1 = ($1_ltype)&temp;
}
%typemap(java, in) enum SWIGTYPE *OUTENUM ($*1_type temp) {
    $1 = ($1_ltype)&temp;
}
%typemap(python, argout, fragment="t_output_helper") enum SWIGTYPE *OUTENUM {
    $result = t_output_helper($result, PyInt_FromLong(*$1));
}
#ifdef SWIGPERL
%apply long *OUTPUT { enum SWIGTYPE *OUTENUM };
#endif

/* -----------------------------------------------------------------------
   Create a typemap for specifying string args that may be NULL.
*/
%typemap(python, in, parse="z") const char *MAY_BE_NULL "";

%typemap(java, in) const char *MAY_BE_NULL { 
  /* ### WHEN IS THIS USED? */
  $1 = 0;
  if ($input) {
    $1 = ($1_ltype)JCALL2(GetStringUTFChars, jenv, $input, 0);
    if (!$1) return $null;
  }
}

#ifdef SWIGPERL
%apply const char * { const char *MAY_BE_NULL };
#endif

%typemap(perl5,out) svn_error_t * {
    if ($1) {
	char buf[128], *p;
	SV *err = sv_newmortal();
	p = svn_strerror($1->apr_err, buf, 128);
	sv_setpv (err, p);
	while ($1) {
	    sv_catpvf (err, ": %s", $1->message);
	    $1 = $1->child;
	}
	croak (SvPV_nolen(err));
    }
}

/* -----------------------------------------------------------------------
   Define a more refined 'varin' typemap for 'const char *' members. This
   is used in place of the 'char *' handler defined automatically.

   We need to do the free/malloc/strcpy special because of the const
*/
%typemap(memberin) const char * {
    apr_size_t len = strlen($input) + 1;
    char *copied;
    if ($1) free((char *)$1);
    copied = malloc(len);
    memcpy(copied, $input, len);
    $1 = copied;
}

/* -----------------------------------------------------------------------
   Specify how svn_error_t returns are turned into exceptions.
*/

%typemap(python, out) svn_error_t * {
    if ($1 != NULL) {
        if ($1->apr_err != SVN_ERR_SWIG_PY_EXCEPTION_SET)
            PyErr_SetString(PyExc_RuntimeError,
                            $1->message ? $1->message : "unknown error");
        return NULL;
    }
    Py_INCREF(Py_None);
    $result = Py_None;
}

%typemap(java, out) svn_error_t * %{
    $result = ($1 != NULL) ? svn_swig_java_convert_error(jenv, $1) : NULL;
%}
%typemap(jni) svn_error_t * "jthrowable"
%typemap(jtype) svn_error_t * "org.tigris.subversion.SubversionException"
%typemap(jstype) svn_error_t * "org.tigris.subversion.SubversionException"
%typemap(javain) svn_error_t * "@javainput"
%typemap(javaout) svn_error_t * {
	return $jnicall;
}

/* Make the proxy classes much more usable */
%typemap(javaptrconstructormodifiers) SWIGTYPE, SWIGTYPE *, SWIGTYPE &, SWIGTYPE [] "public"

/* -----------------------------------------------------------------------
   'svn_renum_t *' will always be an OUTPUT parameter
*/
%apply long *OUTPUT { svn_revnum_t * };

/* -----------------------------------------------------------------------
   Define an OUTPUT typemap for 'svn_filesize_t *'.  For now, we'll
   treat it as a 'long' even if that isn't entirely correct...  
*/
%typemap(python,in,numinputs=0) svn_filesize_t * (svn_filesize_t temp)
    "$1 = &temp;";

%typemap(python,argout,fragment="t_output_helper") svn_filesize_t *
    "$result = t_output_helper($result,PyInt_FromLong((long) (*$1)));";

%apply long *OUTPUT { svn_filesize_t * };

/* -----------------------------------------------------------------------
   Define a general ptr/len typemap. This takes a single script argument
   and expands it into a ptr/len pair for the native call.
*/
%typemap(python, in) (const char *PTR, apr_size_t LEN) {
    if (!PyString_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "expecting a string");
        return NULL;
    }
    $1 = PyString_AS_STRING($input);
    $2 = PyString_GET_SIZE($input);
}

%typemap(java, in) (const char *PTR, apr_size_t LEN) (char c) {
    if ($input != NULL) {
	    /* Do not use GetPrimitiveArrayCritical and ReleasePrimitiveArrayCritical
		* since the Subversion client might block the thread */

       $1 = JCALL2(GetByteArrayElements, jenv, $input, NULL);
	   $2 = JCALL1(GetArrayLength, jenv, $input);
	}
	else {
       $1 = &c;
	   $2 = 0;
	}
}

%typemap(java, freearg) (const char *PTR, apr_size_t LEN) {
	if ($input != NULL) {
           JCALL3(ReleaseByteArrayElements, jenv, $input, $1, JNI_ABORT);
        }
	/* Since this buffer is used as input JNI_ABORT is safe as "mode" above*/
}

%typemap(jni) (const char *PTR, apr_size_t LEN) "jbyteArray"
%typemap(jtype) (const char *PTR, apr_size_t LEN) "byte[]"
%typemap(jstype) (const char *PTR, apr_size_t LEN) "byte[]"
%typemap(javain) (const char *PTR, apr_size_t LEN) "$javainput"
%typemap(javaout) (const char *PTR, apr_size_t LEN) {
    return $jnicall;
  }

%typemap(perl5, in) (const char *PTR, apr_size_t LEN) {
    /* ### FIXME-perl ptr/len */
}
/* -----------------------------------------------------------------------
   Define a generic arginit mapping for pools.
*/

%typemap(python, arginit) apr_pool_t *pool(apr_pool_t *_global_pool) {
    /* Assume that the pool here is the last argument in the list */
    SWIG_ConvertPtr(PyTuple_GET_ITEM(args, PyTuple_GET_SIZE(args) - 1),
                    (void **)&$1, $1_descriptor, SWIG_POINTER_EXCEPTION | 0);
    _global_pool = $1;
}

%typemap(perl5, in) apr_pool_t *pool "";

%typemap(perl5, default) apr_pool_t *pool(apr_pool_t *_global_pool) {
    SV *pool = ST(items-1);
    if (pool && sv_isobject(pool) && sv_derived_from(pool, "_p_apr_pool_t")) {
	SWIG_ConvertPtr(ST(items-1), (void **)&$1, $1_descriptor, 0);
	current_pool = $1;
    }
    else {
	if (current_pool)
	    $1 = current_pool;
	else {
	    apr_pool_create(&$1, NULL);
	    current_pool = $1;
	}
    }
    _global_pool = $1;
}

%typemap(perl5, freearg) apr_pool_t *pool {
    /*
    if (_global_pool_flag)
	apr_pool_destroy(_global_pool);
    */
}

%typemap(java, arginit) apr_pool_t *pool(apr_pool_t *_global_pool) {
    /* ### HACK: Get the input variable based on naming convention */
	_global_pool = *(apr_pool_t **)&j$1;
	$1 = 0;
}

/* -----------------------------------------------------------------------
   result of check_path
*/
%apply enum SWIGTYPE *OUTENUM { svn_node_kind_t * };

/* -----------------------------------------------------------------------
   get_logs takes a callback function, so we have to thunk it
*/

%typemap(python, in) (svn_log_message_receiver_t receiver, 
                      void *receiver_baton) {
    $1 = svn_swig_py_thunk_log_receiver;
    $2 = (void *)$input;
}
%typemap(perl5, in) (svn_log_message_receiver_t receiver, 
                      void *receiver_baton) {
    $1 = svn_swig_pl_thunk_log_receiver;
    $2 = (void *)$input;
}

/* stream_t * */
%apply SWIGTYPE **OUTPARAM { svn_stream_t ** };

/* -----------------------------------------------------------------------
  useful convertors for svn_opt_revision_t
*/
%typemap(perl5, in) svn_opt_revision_t * (svn_opt_revision_t rev) {
    $1 = &rev;
    if ($input == NULL || $input == &PL_sv_undef) {
	rev.kind = svn_opt_revision_unspecified;
    }
    else if (sv_isobject($input) && sv_derived_from($input, "_p_svn_opt_revision_t")) {
	SWIG_ConvertPtr($input, (void **)&$1, $1_descriptor, 0);
    }
    else if (SvIOK($input)) {
	rev.kind = svn_opt_revision_number;
	rev.value.number = SvIV($input);
    }
    else {
	char *input = SvPV_nolen($input);
	if (strcasecmp(input, "BASE") == 0)
	    rev.kind = svn_opt_revision_base;
	else if (strcasecmp(input, "HEAD") == 0)
	    rev.kind = svn_opt_revision_head;
	else
	    SWIG_croak("unknown opt_revison_t type");
    }

}

/* -----------------------------------------------------------------------
   Special boolean mapping for java.
*/
%typemap(java, jni) svn_boolean_t "jboolean";
%typemap(java, jtype) svn_boolean_t "boolean";
%typemap(java, jstype) svn_boolean_t "boolean";
%typemap(java, in) svn_boolean_t %{
    $1 = $input ? TRUE : FALSE;
%}
%typemap(java, out) svn_boolean_t %{
    $result = $1 ? JNI_TRUE : JNI_FALSE;
%}

/* -----------------------------------------------------------------------
   Handle python thread locking.

   Swig doesn't allow us to specify a language in the %exception command,
   so we have to use #ifdefs for the python-specific parts.
*/

%exception {
#ifdef SWIGPYTHON
    release_py_lock();
#endif
    $action
#ifdef SWIGPYTHON
    acquire_py_lock();
#endif
}

/* ----------------------------------------------------------------------- */

%include svn_types.h

%header %{
#include "svn_types.h"

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
