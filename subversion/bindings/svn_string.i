/*
 * svn_string.i :  SWIG interface file for svn_string.h
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* This interface file does not include a %module line because it should
   only be imported by other modules. */

%import apr.i
%import svn_types.i

typedef struct svn_stringbuf_t svn_stringbuf_t;

%typemap(python,in) svn_stringbuf_t * {
    if (!PyString_Check($source)) {
        PyErr_SetString(PyExc_TypeError, "not a string");
        return NULL;
    }
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

// ### nothing to do right now
