/*
 * apr.i :  SWIG interface file for selected APR types
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
   the various types in here, as necessary. */
/* ### actually, we may need to wrap some things, such as apr_initialize() */

%include "typemaps.i"

/* ----------------------------------------------------------------------- */

/* 'apr_off_t *' will always be an OUTPUT parameter */
%typemap(in) apr_off_t * = long *OUTPUT;
%typemap(ignore) apr_off_t * = long *OUTPUT;
%typemap(argout) apr_off_t * = long *OUTPUT;

/* ----------------------------------------------------------------------- */

%include apr.h

/* ### be nice to have all the error values and macros. there are some
   ### problems including this file, tho. SWIG isn't smart enough with some
   ### of the preprocessing and thinks there is a macro redefinition */
//%include apr_errno.h
typedef int apr_status_t;

/* ### seems that SWIG isn't picking up the definition of size_t */
typedef unsigned long size_t;

/* Define the time type (rather than picking up all of apr_time.h) */
typedef apr_int64_t apr_time_t;

/* -----------------------------------------------------------------------
   handle long long values so that apr_time_t works well
*/

%typemap(python,in) long long {
    $target = PyLong_AsLongLong($source);
}

/* 'long long *' will always be an OUTPUT parameter */
%typemap(ignore) long long * (long long temp) {
    $target = &temp;
}
%typemap(python,argout) long long * {
    $target = t_output_helper($target, PyLong_FromLongLong(*$source));
}

/* deal with return a return value */
%typemap(python,out) long long {
    $target = t_output_helper($target, PyLong_FromLongLong($source));
}

/* -----------------------------------------------------------------------
   create some INOUT typemaps for apr_size_t
*/

%typemap(in) apr_size_t *INOUT = unsigned long *INOUT;
%typemap(argout) apr_size_t *INOUT = unsigned long *INOUT;

/* -----------------------------------------------------------------------
   create an OUTPUT argument defn for an apr_hash_t ** which is storing
   property values
*/

%typemap(ignore) apr_hash_t **PROPHASH (apr_hash_t *temp) {
    $target = &temp;
}
%typemap(python,argout) apr_hash_t **PROPHASH {
    /* toss prior result, get new result from the hash */
    Py_DECREF($target);
    $target = prophash_to_dict(*$source);
}

/* -----------------------------------------------------------------------
   apr_file_t ** is always an OUT param
*/

%typemap(ignore) apr_file_t ** (apr_file_t *temp) {
    $target = &temp;
}
%typemap(python,argout) apr_file_t ** {
    $target = t_output_helper($target,
                              SWIG_NewPointerObj(*$source,
                                                 SWIGTYPE_p_apr_file_t));
}

/* -----------------------------------------------------------------------
   Implement some various utility functions

   prophash_to_dict: convert an apr_hash_t * into a Python dict
*/

#ifdef SWIGPYTHON
%header %{
/* helper function to convert an apr_hash_t* (char* -> svnstring_t*) to
   a Python dict */
static PyObject *prophash_to_dict(apr_hash_t *hash)
{
    apr_hash_index_t *hi;
    PyObject *item;
    PyObject *result = PyDict_New();

    if (result == NULL)
        return NULL;

    for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi)) {
        const void *key;
        void *val;
        const svn_string_t *propval;

        apr_hash_this(hi, &key, NULL, &val);
        propval = val;
        /* ### borrowing from value in the pool. or should we copy? note
           ### that copying is "safest" */
        item = PyBuffer_FromMemory(propval->data, propval->len);
        /* item = PyString_FromStringAndSize(propval->data, propval->len); */
        if (item == NULL)
            goto error2;
        if (PyDict_SetItemString(result, key, item) == -1)
            goto error1;
        /* ### correct? or does SetItemString take this? */
        Py_DECREF(item);
    }

    return result;

  error1:
    Py_DECREF(item);
  error2:
    Py_DECREF(result);
    return NULL;
}
%}
#endif /* SWIGPYTHON */
