/*
 * svn_string.i :  SWIG interface file for svn_string.h
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
%typemap(python,argout,fragment="t_output_helper") RET_STRING {
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
%typemap(java,out) RET_STRING {
    /* FIXME: This is just a stub -- implement JNI code for returning a string! */
    $output = NULL;
}

%typemap(jni) char *                                         "jstring"

%typemap(perl5,argout) RET_STRING {
    if (*$1) {
	$result = sv_newmortal();
	sv_setpvn ($result, (*$1)->data, (*$1)->len);
    }
    else
	$result = &PL_sv_undef;
    argvi++;
}
/* -----------------------------------------------------------------------
   TYPE: svn_stringbuf_t
*/

%typemap(python,in) svn_stringbuf_t * {
    if (!PyString_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "not a string");
        return NULL;
    }
    $1 = svn_stringbuf_ncreate(PyString_AS_STRING($input),
                               PyString_GET_SIZE($input),
                               /* ### gah... what pool to use? */
                               _global_pool);
}

%typemap(perl5,in) svn_stringbuf_t * {
    /* ### FIXME-perl */
}
%typemap(python,out) svn_stringbuf_t * {
    $result = PyString_FromStringAndSize($1->data, $1->len);
}
%typemap(perl5,out) svn_stringbuf_t * {
    /* ### FIXME-perl */
}

/* svn_stringbuf_t ** is always an output parameter */
%typemap(python,in,numinputs=0) svn_stringbuf_t ** (svn_stringbuf_t *temp) {
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
%typemap(perl5,in) const svn_string_t * (svn_string_t value) {
    if ($input == &PL_sv_undef) {
	$1 = NULL;
    }
    else if (SvOK($input)) {
	value.data = SvPV($input, value.len);
	$1 = &value;
    }
    else {
	SWIG_croak("not a string");
    }
}

/* when storing an svn_string_t* into a structure, we must allocate the
   svn_string_t structure on the heap. */
%typemap(python,memberin) const svn_string_t * {
    $1 = svn_string_dup($input, _global_pool);
}
%typemap(perl5,memberin) const svn_string_t * {
    $1 = svn_string_dup($input, _global_pool);
}

%typemap(python,out) svn_string_t * {
    $result = PyString_FromStringAndSize($1->data, $1->len);
}
%typemap(perl5,out) svn_string_t * {
    $result = sv_2mortal(newSVpv($1->data, $1->len));
    ++argvi;
}

/* svn_string_t ** is always an output parameter */
%typemap(in,numinputs=0) svn_string_t ** (svn_string_t *temp) {
    $1 = &temp;
}
%apply RET_STRING { svn_string_t ** };



/* -----------------------------------------------------------------------
   define a way to return a 'const char *'
*/

/* ### note that SWIG drops the const in the arg decl, so we must cast */
%typemap(in, numinputs=0) const char **OUTPUT (const char *temp = NULL)
    "$1 = (char **)&temp;"

%typemap(python,argout,fragment="t_output_helper") const char **OUTPUT {
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

%typemap(perl5,argout) const char **OUTPUT {
    if (*$1 == NULL)
	$result = &PL_sv_undef;
    else
	$result = sv_2mortal(newSVpv(*$1, 0));
    ++argvi;
}
/* -----------------------------------------------------------------------
   define a general INPUT param of an array of svn_stringbuf_t* items.
 */

%typemap(python,in) const apr_array_header_t *STRINGLIST {
    $1 = (apr_array_header_t *) svn_swig_py_strings_to_array($input,
                                                             _global_pool);
    if ($1 == NULL)
        return NULL;
}
%typemap(perl5,in) const apr_array_header_t *STRINGLIST {
    $1 = (apr_array_header_t *) svn_swig_pl_strings_to_array($input,
                                                             _global_pool);
    if ($1 == NULL)
        return NULL;
}

%typemap(jni) const apr_array_header_t *STRINGLIST "jobjectArray"
%typemap(jtype) const apr_array_header_t *STRINGLIST "java.lang.String[]"
%typemap(jstype) const apr_array_header_t *STRINGLIST "java.lang.String[]"
%typemap(javain) const apr_array_header_t *STRINGLIST "$javainput"

%typemap(java,in) const apr_array_header_t *STRINGLIST (apr_array_header_t *temp) {
	temp = (apr_array_header_t *)svn_swig_java_strings_to_array(jenv, $input, _global_pool);
	$1 = temp;
}

%typemap(java,freearg) const apr_array_header_t *STRINGLIST {
	/* FIXME: Perhaps free up "temp"? */
}

/* path lists */
%apply const apr_array_header_t *STRINGLIST {
    const apr_array_header_t *paths
};

/* ----------------------------------------------------------------------- */
