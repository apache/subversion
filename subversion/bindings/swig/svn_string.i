/*
 * svn_string.i :  SWIG interface file for svn_string.h
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
   generic OUT param typemap for svn_string(buf)_t. we can share these
   because we only refer to the ->data and ->len values.
*/
%typemap(python,argout) RET_STRING {
    PyObject *s;
    if (*$1 == NULL) {
        Py_INCREF(Py_None);
        s = Py_None;
    }
    else {
        s = PyString_FromStringAndSize((*$1)->data, (*$1)->len);
        if (s == NULL)
            return NULL;
    }
    $result = t_output_helper($result, s);
}

/* -----------------------------------------------------------------------
   TYPE: svn_stringbuf_t
*/

%typemap(python,in) svn_stringbuf_t * {
    if (!PyString_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "not a string");
        return NULL;
    }
%#error need pool argument from somewhere
    $1 = svn_stringbuf_ncreate(PyString_AS_STRING($input),
                               PyString_GET_SIZE($input),
                               /* ### gah... what pool to use? */
                               pool);
}

%typemap(python,out) svn_stringbuf_t * {
    $result = PyString_FromStringAndSize($1->data, $1->len);
}

/* svn_stringbuf_t ** is always an output parameter */
%typemap(ignore) svn_stringbuf_t ** (svn_stringbuf_t *temp) {
    $1 = &temp;
}
%apply RET_STRING { svn_stringbuf_t ** };


/* -----------------------------------------------------------------------
   TYPE: svn_string_t
*/

/* const svn_string_t * is always an input parameter */
%typemap(python,in) const svn_string_t * (svn_string_t value) {
    if ($input == Py_None)
        $1 = NULL;
    else {
        if (!PyString_Check($input)) {
            PyErr_SetString(PyExc_TypeError, "not a string");
            return NULL;
        }
        value.data = PyString_AS_STRING($input);
        value.len = PyString_GET_SIZE($input);
        $1 = &value;
    }
}

/* when storing an svn_string_t* into a structure, we must allocate the
   svn_string_t structure on the heap. */
%typemap(python,memberin) const svn_string_t * {
%#error need pool argument from somewhere
    $1 = svn_string_dup($input, pool);
}


//%typemap(python,out) svn_string_t * {
//    $result = PyBuffer_FromMemory($1->data, $1->len);
//}

/* svn_string_t ** is always an output parameter */
%typemap(ignore) svn_string_t ** (svn_string_t *temp) {
    $1 = &temp;
}
%apply RET_STRING { svn_string_t ** };

/* -----------------------------------------------------------------------
   define a way to return a 'const char *'
*/

/* ### note that SWIG drops the const in the arg decl, so we must cast */
%typemap(ignore) const char **OUTPUT (const char *temp) {
    $1 = (char **)&temp;
}
%typemap(python,argout) const char **OUTPUT {
    PyObject *s;
    if (*$1 == NULL) {
        Py_INCREF(Py_None);
        s = Py_None;
    }
    else {
        s = PyString_FromString(*$1);
        if (s == NULL)
            return NULL;
    }
    $result = t_output_helper($result, s);
}

/* -----------------------------------------------------------------------
   define a general INPUT param of an array of svn_stringbuf_t* items.
 */

%typemap(python,in) const apr_array_header_t *STRINGLIST {
%#error need pool argument from somewhere
    $1 = svn_swig_strings_to_array($input, NULL);
    if ($1 == NULL)
        return NULL;
}

/* ----------------------------------------------------------------------- */
