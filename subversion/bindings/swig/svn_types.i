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
%typemap(python, argout, fragment="t_output_helper") SWIGTYPE **OUTPARAM {
    $result = t_output_helper($result,
                              SWIG_NewPointerObj(*$1, $*1_descriptor, 0));
}

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

/* ----------------------------------------------------------------------- */

/* Define an OUTPUT typemap for 'svn_filesize_t *'.  For now, we'll
   treat it as a 'long' even if that isn't entirely correct...  */

%typemap(python,in,numinputs=0) svn_filesize_t * (svn_filesize_t temp)
    "$1 = &temp;";

%typemap(python,argout,fragment="t_output_helper") svn_filesize_t *
    "$result = t_output_helper($result,PyInt_FromLong((long) (*$1)));";

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

/* -----------------------------------------------------------------------
   Define a generic arginit mapping for pools.
*/

%typemap(python, arginit) apr_pool_t *pool(apr_pool_t *_global_pool) {
    /* Assume that the pool here is the last argument in the list */
    SWIG_ConvertPtr(PyTuple_GET_ITEM(args, PyTuple_GET_SIZE(args) - 1),
                    (void **)&$1, $1_descriptor, SWIG_POINTER_EXCEPTION | 0);
    _global_pool = $1;
}

%typemap(java, arginit) apr_pool_t *pool(apr_pool_t *_global_pool) {
    /* ### HACK: Get the input variable based on naming convention */
	_global_pool = *(apr_pool_t **)&j$1;
	$1 = 0;
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
%}
