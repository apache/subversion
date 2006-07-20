/*
 * svn_string.swg :  SWIG include file for svn_string.h
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

%import apr.swg
%import svn_types.swg

typedef struct svn_stringbuf_t svn_stringbuf_t;
typedef struct svn_string_t svn_string_t;

/* -----------------------------------------------------------------------
   generic OUT param typemap for svn_string(buf)_t. we can share these
   because we only refer to the ->data and ->len values.
*/
%typemap(in, numinputs=0) RET_STRING ($*1_ltype temp) {
    $1 = &temp;
}

%typemap(python,argout,fragment="t_output_helper") RET_STRING {
    PyObject *s;
    if (*$1 == NULL) {
        Py_INCREF(Py_None);
        s = Py_None;
    }
    else {
        s = PyString_FromStringAndSize((*$1)->data, (*$1)->len);
        if (s == NULL)
            SWIG_fail;
    }
    $result = t_output_helper($result, s);
}
%typemap(perl5,argout) RET_STRING {
    if (*$1) {
	$result = sv_newmortal();
	sv_setpvn ($result, (*$1)->data, (*$1)->len);
    }
    else
	$result = &PL_sv_undef;
    argvi++;
}
%typemap(ruby,argout,fragment="output_helper") RET_STRING
{
  if (*$1) {
    $result = output_helper($result, rb_str_new((*$1)->data, (*$1)->len));
  } else {
    $result = output_helper($result, Qnil);
  }
}

%apply RET_STRING {
  svn_string_t **,
  svn_stringbuf_t **
};

/* -----------------------------------------------------------------------
   TYPE: svn_stringbuf_t
*/

%typemap(python,in) svn_stringbuf_t * {
    if (!PyString_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "not a string");
        SWIG_fail;
    }
    $1 = svn_stringbuf_ncreate(PyString_AS_STRING($input),
                               PyString_GET_SIZE($input),
                               /* ### gah... what pool to use? */
                               _global_pool);
}

%typemap(perl5,in) svn_stringbuf_t * {
    apr_size_t len;
    char *buf;

    if (!SvOK($input)) {
        $1 = NULL;
    } else if (SvPOK($input)) {
        buf = SvPV($input, len);
        /* Another case of ugly pool handling, this should use the current
           default pool, or make a new one if it doesn't exist yet */
        $1 = svn_stringbuf_ncreate(buf,len,
                                   svn_swig_pl_make_pool ((SV *)NULL));
    } else {
        croak("Not a string");
    }
}

%typemap(ruby, in) svn_stringbuf_t *
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    $1 = svn_stringbuf_ncreate(StringValuePtr($input),
                               RSTRING($input)->len,
                               _global_pool);
  }
}

%typemap(ruby, in) svn_stringbuf_t *node_name
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    VALUE rb_pool;
    apr_pool_t *pool;

    svn_swig_rb_get_pool(argc, argv, self, &rb_pool, &pool);
        
    $1 = svn_stringbuf_ncreate(StringValuePtr($input),
                               RSTRING($input)->len,
                               pool);
  }
}


%typemap(python,out) svn_stringbuf_t * {
    $result = PyString_FromStringAndSize($1->data, $1->len);
}

%typemap(perl5,out) svn_stringbuf_t * {
    SV *sv = sv_newmortal();
    sv_setpvn(sv,$1->data,$1->len);
    $result = sv;
    argvi++;
}

%typemap(ruby, out) svn_stringbuf_t *
{
  $result = rb_str_new($1->data, $1->len);
}

%typemap(ruby, argout) svn_stringbuf_t *output
{
  $result = rb_str_new($1->data, $1->len);
}

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
            SWIG_fail;
        }
        value.data = PyString_AS_STRING($input);
        value.len = PyString_GET_SIZE($input);
        $1 = &value;
    }
}
%typemap(perl5,in) const svn_string_t * (svn_string_t value) {
    if (SvOK($input)) {
	value.data = SvPV($input, value.len);
	$1 = &value;
    }
    else {
        $1 = NULL;
    }
}
%typemap(ruby,in) const svn_string_t * (svn_string_t value)
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    value.data = StringValuePtr($input);
    value.len = RSTRING($input)->len;
    $1 = &value;
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
%typemap(ruby,memberin) const svn_string_t * {
    $1 = svn_string_dup($input, _global_pool);
}

%typemap(python,out) svn_string_t * {
    $result = PyString_FromStringAndSize($1->data, $1->len);
}
%typemap(perl5,out) svn_string_t * {
    $result = sv_2mortal(newSVpv($1->data, $1->len));
    ++argvi;
}
%typemap(ruby,out) svn_string_t * {
  if ($1) {
    $result = rb_str_new($1->data, $1->len);
  } else {
    $result = Qnil;
  }
}

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
            SWIG_fail;
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

%typemap(ruby,argout,fragment="output_helper") const char **OUTPUT
{
  if (*$1) {
    $result = output_helper($result, rb_str_new2(*$1));
  } else {
    $result = output_helper($result, Qnil);
  }
}

/* -----------------------------------------------------------------------
   define a general INPUT param of an array of const char * items.
 */

%typemap(python,in) const apr_array_header_t *STRINGLIST {
    $1 = (apr_array_header_t *) svn_swig_py_strings_to_array($input,
                                                             _global_pool);
    if ($1 == NULL)
        SWIG_fail;
}
%typemap(perl5,in) const apr_array_header_t *STRINGLIST {
    $1 = (apr_array_header_t *) svn_swig_pl_strings_to_array($input,
                                                             _global_pool);
}

%typemap(ruby,in) const apr_array_header_t *STRINGLIST {
  $1 = svn_swig_rb_strings_to_apr_array($input, _global_pool);
}

/* path lists */
%apply const apr_array_header_t *STRINGLIST {
    const apr_array_header_t *paths
};

/* ----------------------------------------------------------------------- */
