/*
 * swigutil.c: utility functions for the SWIG bindings
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

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_string.h"

#include "swigutil.h"


#ifdef SWIGPYTHON

#include <Python.h>


static PyObject *convert_hash(apr_hash_t *hash,
                              PyObject * (*converter_func)(void *value,
                                                           void *ctx),
                              void *ctx)
{
    apr_hash_index_t *hi;
    PyObject *dict = PyDict_New();

    if (dict == NULL)
        return NULL;

    for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi)) {
        const void *key;
        void *val;
        PyObject *value;

        apr_hash_this(hi, &key, NULL, &val);
        value = (*converter_func)(val, ctx);
        if (value == NULL) {
            Py_DECREF(dict);
            return NULL;
        }
        /* ### gotta cast this thing cuz Python doesn't use "const" */
        if (PyDict_SetItemString(dict, (char *)key, value) == -1) {
            Py_DECREF(value);
            Py_DECREF(dict);
            return NULL;
        }
        Py_DECREF(value);
    }

    return dict;
}

static PyObject *convert_to_swigtype(void *value, void *ctx)
{
  /* ctx is a 'swig_type_info *' */
  return SWIG_NewPointerObj(value, ctx);
}

static PyObject *convert_svn_string_t(void *value, void *ctx)
{
  /* ctx is unused */

  const svn_string_t *s = value;

  /* ### borrowing from value in the pool. or should we copy? note
     ### that copying is "safest" */

  /* ### gotta cast this thing cuz Python doesn't use "const" */
  return PyBuffer_FromMemory((void *)s->data, s->len);
}


PyObject *svn_swig_prophash_to_dict(apr_hash_t *hash)
{
  return convert_hash(hash, convert_svn_string_t, NULL);
}

PyObject *svn_swig_convert_hash(apr_hash_t *hash, swig_type_info *type)
{
  return convert_hash(hash, convert_to_swigtype, type);
}

PyObject *svn_swig_c_strings_to_list(char **strings)
{
    PyObject *list = PyList_New(0);
    char *s;

    while ((s = *strings++) != NULL) {
        PyObject *ob = PyString_FromString(s);

        if (ob == NULL)
            goto error;
        if (PyList_Append(list, ob) == -1)
            goto error;
    }

    return list;

  error:
    Py_DECREF(list);
    return NULL;
}

#endif /* SWIGPYTHON */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
