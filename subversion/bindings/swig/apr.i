/*
 * apr.i :  SWIG interface file for selected APR types
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

/* This is the interface for the APR headers. This is not built as a module
   because we aren't going to wrap the APR functions. Thus, we only define
   the various types in here, as necessary.

   Actually, core.i wraps a few, key functions.
*/

%include typemaps.i

/* -----------------------------------------------------------------------
   This is default in SWIG 1.3.17 and is a really good idea
*/

%typemap(javagetcptr) SWIGTYPE, SWIGTYPE *, SWIGTYPE &, SWIGTYPE [], \
    SWIGTYPE (CLASS::*) %{
  protected static long getCPtr($javaclassname obj) {
    return (obj == null) ? 0 : obj.swigCPtr;
  }
%}

/* ----------------------------------------------------------------------- */

%include apr.h

/* ### be nice to have all the error values and macros. there are some
   ### problems including this file, tho. SWIG isn't smart enough with some
   ### of the preprocessing and thinks there is a macro redefinition */
//%include apr_errno.h
typedef int apr_status_t;

/* -----------------------------------------------------------------------
   apr_time_t
*/

/* Define the time type (rather than picking up all of apr_time.h) */
typedef apr_int64_t apr_time_t;

/* For apr_time_ansi_put().
   We guess, because only the system C compiler can successfully parse
   system headers if they incorporate weird include paths
   (e.g. /usr/lib/gcc-lib/plat/ver/include). */
typedef apr_int32_t time_t;

#if APR_INT64_T_FMT == "ld"
    %apply long *OUTPUT { apr_time_t * };
#else
    %apply long long *OUTPUT { apr_time_t * };
#endif

/* -----------------------------------------------------------------------
   create an OUTPUT argument typemap for an apr_hash_t **
*/

%typemap(python,in,numinputs=0) apr_hash_t **OUTPUT (apr_hash_t *temp)
    "$1 = &temp;";

%typemap(perl5,in,numinputs=0) apr_hash_t **OUTPUT (apr_hash_t *temp)
    "$1 = &temp;";

/* -----------------------------------------------------------------------
   create an OUTPUT argument defn for an apr_hash_t ** which is storing
   property values
*/

%typemap(python,in,numinputs=0) apr_hash_t **PROPHASH = apr_hash_t **OUTPUT;
%typemap(python,argout) apr_hash_t **PROPHASH {
    /* toss prior result, get new result from the hash */
    Py_DECREF($result);
    $result = svn_swig_py_prophash_to_dict(*$1);
}

%typemap(perl5,in,numinputs=0) apr_hash_t **PROPHASH = apr_hash_t **OUTPUT;
%typemap(perl5,argout) apr_hash_t **PROPHASH {
    $result = svn_swig_pl_prophash_to_hash(*$1);
    argvi++;
}

/* -----------------------------------------------------------------------
   Handle an apr_hash_t ** in Java
*/

%typemap(jni) apr_hash_t ** "jobject"
%typemap(jtype) apr_hash_t ** "java.util.Map"
%typemap(jstype) apr_hash_t ** "java.util.Map"
%typemap(javain) apr_hash_t ** "$javainput"

%typemap(javaout) apr_hash_t ** {
    return $jnicall;
  }

%typemap(java,in) apr_hash_t **(apr_hash_t *temp){
    $1 = &temp;
}

%typemap(java,out) apr_hash_t ** {
    svn_swig_java_add_to_map(jenv, *$1, $input);
}

%typemap(java,argout) apr_hash_t ** {
    svn_swig_java_add_to_map(jenv, *$1, $input);
}

%typemap(java,argout) apr_hash_t **PROPHASH {
    svn_swig_java_add_to_map(jenv, *$1, $input);
}

/* -----------------------------------------------------------------------
   Handle an apr_array_header_t * in Java
*/

%typemap(jni) apr_array_header_t * "jobject"
%typemap(jtype) apr_array_header_t * "java.util.List"
%typemap(jstype) apr_array_header_t * "java.util.List"
%typemap(javain) apr_array_header_t * "$javainput"

%typemap(javaout) apr_array_header_t * {
    return $jnicall;
  }

%typemap(java, argout) apr_array_header_t * {
    svn_swig_java_add_to_list(jenv, $1, $input);
}

/* -----------------------------------------------------------------------
  handle apr_file_t *
*/

%typemap(python, in) apr_file_t * {
  $1 = svn_swig_py_make_file($input, _global_pool);
}

%typemap(perl5, in) apr_file_t * {
  $1 = svn_swig_pl_make_file($input, _global_pool);
}

/* -----------------------------------------------------------------------
   apr_file_t ** is always an OUT param
*/

%typemap(in, numinputs=0) apr_file_t ** (apr_file_t *temp)
    "$1 = &temp;";

%typemap(python,argout,fragment="t_output_helper") apr_file_t **
    "$result = t_output_helper(
        $result,
        SWIG_NewPointerObj(*$1, $*1_descriptor, 0));";

%typemap(perl5, argout) apr_file_t ** {
    ST(argvi) = sv_newmortal();
    SWIG_MakePtr(ST(argvi++), (void *)*$1, $*1_descriptor,0);
}

/* ----------------------------------------------------------------------- */
