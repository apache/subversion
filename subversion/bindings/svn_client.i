/*
 * svn_client.i :  SWIG interface file for svn_client.h
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

%module _client

%import apr.i
%import svn_types.i
%import svn_string.i
%import svn_delta.i

/* -----------------------------------------------------------------------
   all "targets" arrays are constant inputs of svn_stringbuf_t*
 */

%typemap(python,in) const apr_array_header_t *targets {
%#error need pool argument from somewhere
    $target = strings_to_array($source, NULL);
    if ($target == NULL)
        return NULL;
}

/* -----------------------------------------------------------------------
   svn_client_propget's apr_hash_t **props returns a dictionary
 */

%typemap(ignore) apr_hash_t **props (apr_hash_t *temp) {
    $target = &temp;
}
%typemap(python,argout) apr_hash_t **props {
    /* toss prior result, get new result from the hash */
    Py_DECREF($target);
    $target = hash_to_dict(*$source);
}

/* ----------------------------------------------------------------------- */

%include svn_client.h
%{
#include "svn_client.h"
%}

// ### how do we make this code Python only? #ifdef SWIGPYTHON ??
%{
/* helper function to convert a Python sequence of strings into an
   apr_array_header_t* of svn_stringbuf_t* objects. */
static const apr_array_header_t *strings_to_array(PyObject *source,
                                                  apr_pool_t *pool)
{
    int targlen;
    apr_array_header_t *temp;

    if (!PySequence_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "not a sequence");
        return NULL;
    }
    targlen = PySequence_Length(source);
    temp = apr_array_make(pool, targlen, sizeof(svn_stringbuf_t *));
    while (targlen--) {
        PyObject *o = PySequence_GetItem(source, targlen);
        if (o == NULL)
            return NULL;
        if (!PyString_Check(o)) {
            Py_DECREF(o);
            PyErr_SetString(PyExc_TypeError, "not a sequence");
            return NULL;
        }
        APR_ARRAY_IDX(temp, targlen, svn_stringbuf_t *) =
            svn_stringbuf_ncreate(PyString_AS_STRING(o),
                                  PyString_GET_SIZE(o),
                                  pool);
        Py_DECREF(o);
    }
    return temp;
}

/* helper function to convert an apr_hash_t* to a Python dict */
static PyObject *hash_to_dict(apr_hash_t *hash)
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
        if (PyDict_SetItemString(result, key, item))
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
