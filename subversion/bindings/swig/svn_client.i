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
   don't wrap the following items
*/
%ignore svn_client_proplist_item_s;

/* -----------------------------------------------------------------------
   all "targets" arrays are constant inputs of svn_stringbuf_t*
 */
%typemap(in) const apr_array_header_t *targets =
    const apr_array_header_t *STRINGLIST;

/* -----------------------------------------------------------------------
   fix up the return hash for svn_client_propget()
*/
%typemap(ignore) apr_hash_t **props = apr_hash_t **PROPHASH;
%typemap(argout) apr_hash_t **props = apr_hash_t **PROPHASH;

/* -----------------------------------------------------------------------
   handle the return value for svn_client_proplist()
*/

%typemap(ignore) apr_array_header_t ** (apr_array_header_t *temp) {
    $target = &temp;
}
%typemap(python,argout) apr_array_header_t ** {
    svn_client_proplist_item_t **ppitem;
    int i;
    int nelts = (*$source)->nelts;
    PyObject *list = PyList_New(i);
    if (list == NULL)
        return NULL;
    ppitem = (svn_client_proplist_item_t **)(*$source)->elts;
    for (i = 0; i < nelts; ++ppitem) {
        PyObject *item = PyTuple_New(2);
        PyObject *name = PyString_FromStringAndSize((*ppitem)->node_name->data,
                                                    (*ppitem)->node_name->len);
        PyObject *hash = prophash_to_dict((*ppitem)->prop_hash);

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
    $target = t_output_helper($target, list);
}

/* -----------------------------------------------------------------------
   the "diff_options" in svn_client_diff() is a STRINGLIST, too
*/
%typemap(in) const apr_array_header_t *diff_options =
    const apr_array_header_t *STRINGLIST;

/* -----------------------------------------------------------------------
   handle the "statushash" OUTPUT param for svn_client_status()
*/
%typemap(ignore) apr_hash_t **statushash (apr_hash_t *temp) {
    $target = &temp;
}
%typemap(python,argout) apr_hash_t **statushash {
    apr_hash_index_t *hi;
    PyObject *dict = PyDict_New();

    if (dict == NULL)
        return NULL;

    for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi)) {
        const void *key;
        void *val;
        PyObject *value;

        apr_hash_this(hi, &key, NULL, &val);
        /* ### how to ensure this type is registered? */
        value = SWIG_NewPointerObj(val, SWIGTYPE_p_svn_wc_status_t);
        if (value == NULL) {
            Py_DECREF(dict);
            return NULL;
        }
        if (PyDict_SetItemString(dict, key, value) == -1) {
            Py_DECREF(value);
            Py_DECREF(dict);
            return NULL;
        }
        /* ### correct? or does SetItemString take this? */
        Py_DECREF(value);
    }

    $target = t_output_helper($target, list);
}

/* ----------------------------------------------------------------------- */

%include svn_client.h
%header %{
#include "svn_client.h"
%}
