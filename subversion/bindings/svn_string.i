/*
 * svn_string.i :  SWIG interface file for svn_string.h
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

/* This interface file does not include a %module line because it should
   only be imported by other modules. */

%import apr.i
%import svn_types.i

typedef struct svn_stringbuf_t svn_stringbuf_t;
typedef struct svn_string_t svn_string_t;

/* -----------------------------------------------------------------------
   TYPE: svn_stringbuf_t
*/

%typemap(python,in) svn_stringbuf_t * {
    if (!PyString_Check($source)) {
        PyErr_SetString(PyExc_TypeError, "not a string");
        return NULL;
    }
%#error need pool argument from somewhere
    $target = svn_string_ncreate(PyString_AS_STRING($source),
                                 PyString_GET_SIZE($source),
                                 /* ### gah... what pool to use? */
                                 pool);
}

%typemap(python,out) svn_stringbuf_t * {
    $target = PyString_FromStringAndSize($source->data, $source->len);
}

// ### doesn't seem to work
//svn_stringbuf_t **OUTPUT;
%typemap(ignore) svn_stringbuf_t ** (svn_stringbuf_t *temp) {
    $target = &temp;
}
%typemap(python, argout) svn_stringbuf_t ** {
    $target = t_output_helper($target,
                              PyString_FromStringAndSize((*$source)->data,
							 (*$source)->len));
}


/* -----------------------------------------------------------------------
   TYPE: svn_string_t
*/

/* const svn_string_t * is always an input parameter */
%typemap(python,in) const svn_string_t * (svn_string_t value) {
    if (!PyString_Check($source)) {
        PyErr_SetString(PyExc_TypeError, "not a string");
        return NULL;
    }
    value.data = PyString_AS_STRING($source);
    value.len = PyString_GET_SIZE($source);
    $target = &value;
}

//%typemap(python,out) svn_string_t * {
//    $target = PyString_FromStringAndSize($source->data, $source->len);
//}
