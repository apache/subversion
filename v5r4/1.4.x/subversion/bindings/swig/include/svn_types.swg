/*
 * svn_types.swg :  SWIG include file for svn_types.h
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

/* This interface file only defines types and their related information.
   There is no module associated with this interface file. */

%include apr.swg
%include exception.i

#ifdef SWIGRUBY
#  if SWIG_VERSION <= 0x010327
/* Remove compiler warning (break strict-aliasing rules) */
%typemap(in) SWIGTYPE * (void *temp)
{
#    if SWIG_VERSION <= 0x010325
  SWIG_ConvertPtr($input, &temp, $1_descriptor, 1);
#    else
  SWIG_ConvertPtr($input, &temp, $1_descriptor, $disown);
#    endif
  $1 = ($1_ltype)temp;
}
#  endif
#endif

/* -----------------------------------------------------------------------
   Create a typemap to define "type **" as OUT parameters.

   Note: SWIGTYPE is just a placeholder for "some arbitrary type". This
         typemap will be applied onto a "real" type.
*/
%typemap(python, in, numinputs=0) SWIGTYPE **OUTPARAM ($*1_type temp) {
    $1 = ($1_ltype)&temp;
}
%typemap(perl5, in, numinputs=0) SWIGTYPE **OUTPARAM ($*1_type temp) {
    $1 = ($1_ltype)&temp;
}
%typemap(ruby, in, numinputs=0) SWIGTYPE **OUTPARAM ($*1_type temp) {
  temp = NULL;
  $1 = ($1_ltype)&temp;
}

%typemap(python, argout, fragment="t_output_helper") SWIGTYPE **OUTPARAM {
    $result = t_output_helper($result,
      svn_swig_NewPointerObj(*$1, $*1_descriptor, _global_svn_swig_py_pool));
}
%typemap(perl5, argout) SWIGTYPE **OUTPARAM {
    ST(argvi) = sv_newmortal();
    SWIG_MakePtr(ST(argvi++), (void *)*$1, $*1_descriptor,0);
}
%typemap(ruby, argout, fragment="output_helper") SWIGTYPE **OUTPARAM {
  $result = output_helper($result, SWIG_NewPointerObj(*$1, $*1_descriptor, 0));
}

/* -----------------------------------------------------------------------
   %apply-ing of typemaps
*/

%apply SWIGTYPE **OUTPARAM {
  svn_stream_t **,
  svn_commit_info_t **
};

%apply long *OUTPUT { svn_revnum_t * };
%apply int *OUTPUT { svn_boolean_t * };

/* svn_fs_check_path() */
%apply long *OUTPUT { svn_node_kind_t * };

/* svn_config_get_server_setting_int() */
%apply long long *OUTPUT { apr_int64_t * };

/* -----------------------------------------------------------------------
   Create a typemap for specifying string args that may be NULL.
*/
%typemap(python, in, parse="z") const char *MAY_BE_NULL "";

#ifdef SWIGPERL
%apply const char * { const char *MAY_BE_NULL };
#endif

%typemap(ruby, in) const char* MAY_BE_NULL
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    $1 = StringValuePtr($input);
  }
}

%typemap(ruby, out) const char *
{
  if ($1) {
    $result = rb_str_new2($1);
  } else {
    $result = Qnil;
  }
}

/* -----------------------------------------------------------------------
   const char *header_encoding
   svn_diff_file_output_unified2
   svn_client_diff3
*/
%typemap(ruby, in) const char *header_encoding
{
  $1 = NULL;
  
  if (NIL_P($input)) {
  } else if (TYPE($input) == T_FIXNUM) {
    $1 = (char *)NUM2INT($input);
    if (!($1 == APR_LOCALE_CHARSET || $1 == APR_DEFAULT_CHARSET)) {
      $1 = NULL;
    }
  } else {
    $1 = StringValuePtr($input);
  }
  
  if (!$1) {
    $1 = (char *)APR_LOCALE_CHARSET;
  }
}

/* -----------------------------------------------------------------------
   Input of scripting hash/dictionary (string => string)
   converted to apr_hash_t (const char * => const char *)
*/
%typemap(python, in) apr_hash_t *STRING_TO_STRING {
    $1 = svn_swig_py_stringhash_from_dict ($input, _global_pool);
}
%typemap(perl5, in) apr_hash_t *STRING_TO_STRING {
    $1 = svn_swig_pl_strings_to_hash ($input, _global_pool);
}
%typemap(ruby, in) apr_hash_t *STRING_TO_STRING {
    $1 = svn_swig_rb_hash_to_apr_hash_string($input, _global_pool);
}


/* -----------------------------------------------------------------------
   Define a more refined 'memberin' typemap for 'const char *' members. This
   is used in place of the 'char *' handler defined automatically.

   We need to do the free/malloc/strcpy special because of the const
*/
%typemap(memberin) const char * {
    apr_size_t len = strlen($input) + 1;
    char *copied;
    if ($1) free((char *)$1);
    copied = malloc(len);
    memcpy(copied, $input, len);
    $1 = copied;
}

/* -----------------------------------------------------------------------
   Specify how svn_error_t returns are turned into exceptions.
*/
%typemap(python, out) svn_error_t * {
    if ($1 != NULL) {
        if ($1->apr_err != SVN_ERR_SWIG_PY_EXCEPTION_SET)
            svn_swig_py_svn_exception($1);
        else
            svn_error_clear($1);
        SWIG_fail;
    }
    Py_INCREF(Py_None);
    $result = Py_None;
}

%typemap(perl5,out) svn_error_t * {
    if ($1) {
        SV *exception_handler = perl_get_sv ("SVN::Error::handler", FALSE);

        if (SvOK(exception_handler)) {
            SV *callback_result;

            svn_swig_pl_callback_thunk (CALL_SV, exception_handler,
                                        &callback_result, "S", $1,
                                        $1_descriptor);
        } else {
            $result = sv_newmortal();
            SWIG_MakePtr ($result, (void *)$1, $1_descriptor ,0);
            argvi++;
        }
    }
}

%typemap(ruby, out) svn_error_t *
{
  if ($1) {
    svn_swig_rb_pop_pool(_global_svn_swig_rb_pool);
    svn_swig_rb_handle_svn_error($1);
  }
  $result = Qnil;
}

/* -----------------------------------------------------------------------
   Define an OUTPUT typemap for 'svn_filesize_t *'
*/
%apply long long *OUTPUT { svn_filesize_t * }

/* -----------------------------------------------------------------------
   Define a general ptr/len typemap. This takes a single script argument
   and expands it into a ptr/len pair for the native call.
*/
%typemap(python, in) (const char *PTR, apr_size_t LEN) {
    if (!PyString_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "expecting a string");
        SWIG_fail;
    }
    $1 = PyString_AS_STRING($input);
    $2 = PyString_GET_SIZE($input);
}

%typemap(perl5, in) (const char *PTR, apr_size_t LEN) {
    if (SvPOK($input)) {
        $1 = SvPV($input, $2);
    } else {
        /* set to 0 to avoid warning */
        $1 = 0;
        $2 = 0;
        SWIG_croak("Expecting a string");
    }
}

/* -----------------------------------------------------------------------
   Handle retrieving the error message from svn_strerror
*/

%typemap(perl5,in,numinputs=0) (char *buf, apr_size_t bufsize) ( char temp[128] ) {
    memset (temp,0,128); /* paranoia */
    $1 = temp;
    $2 = 128;
}

/* -----------------------------------------------------------------------
   Define a generic arginit mapping for pools.
*/

#ifdef SWIGPYTHON
%define svn_swig_py_pool_check(pool)
  if (pool != NULL && !PyObject_HasAttrString(pool, (char *)"_mark_valid"))
  {
    int argnum = PyTuple_GET_SIZE(args);
    SWIG_Python_TypeError(SWIG_TypePrettyName($descriptor), pool);
    SWIG_arg_fail(argnum);
    SWIG_fail;
  }
%enddef
%define svn_swig_py_pool_create_subpool(py_pool, pool)
  if (py_pool != NULL) {
    pool = svn_pool_create(pool);
    py_pool = svn_swig_NewPointerObj(pool, $descriptor, py_pool);
  }
%enddef
#endif

%typemap(python, default) apr_pool_t *(apr_pool_t *_global_pool,
  PyObject *_global_svn_swig_py_pool, int _global_pool_is_default_pool)
{
  int argnum = PyTuple_GET_SIZE(args) - 1;
  _global_pool_is_application_pool = 0;
  _global_pool_is_default_pool = 1;
  if (argnum >= 0) {
    PyObject *input = PyTuple_GET_ITEM(args, argnum);
    if (input != Py_None &&
        PyObject_HasAttrString(input, (char *)"_mark_valid")) {
      _global_pool = svn_swig_MustGetPtr(input, $1_descriptor, argnum+1, NULL);
      if (PyErr_Occurred()) {
        _global_svn_swig_py_pool = NULL;
        SWIG_fail;
      }
      _global_svn_swig_py_pool = input;
      Py_XINCREF(_global_svn_swig_py_pool);
      _global_pool_is_default_pool = 0;
    } else {
      if (PyErr_Occurred()) {
        PyErr_Clear();
      }
      svn_swig_get_application_pool(&_global_svn_swig_py_pool, &_global_pool); 
      svn_swig_py_pool_create_subpool(_global_svn_swig_py_pool, _global_pool);
    }
  } else {
    svn_swig_get_application_pool(&_global_svn_swig_py_pool, &_global_pool); 
    svn_swig_py_pool_create_subpool(_global_svn_swig_py_pool, _global_pool);
  }
  $1 = _global_pool;
  svn_swig_py_pool_check(_global_svn_swig_py_pool)
}

%typemap(python, in) apr_pool_t * {
  if ($input != Py_None && _global_pool_is_default_pool == 1) {
    svn_swig_py_pool_check($input)
    _global_pool = svn_swig_MustGetPtr($input, $1_descriptor, $svn_argnum, 
      NULL);
    if (PyErr_Occurred()) {
        SWIG_fail;
    }
    Py_XDECREF(_global_svn_swig_py_pool);
    _global_svn_swig_py_pool = $input;
    Py_XINCREF(_global_svn_swig_py_pool);
    $1 = _global_pool;
  }
}

%typemap(python, freearg) apr_pool_t * {
  Py_XDECREF(_global_svn_swig_py_pool);
}

%typemap(perl5, in) apr_pool_t *pool "";
%typemap(perl5, default) apr_pool_t *pool(apr_pool_t *_global_pool) {
    _global_pool = $1 = svn_swig_pl_make_pool (ST(items-1));
}
%typemap(ruby, in) apr_pool_t *pool "";
%typemap(ruby, default) apr_pool_t *pool
    (VALUE _global_svn_swig_rb_pool, apr_pool_t *_global_pool)
{
  svn_swig_rb_get_pool(argc, argv, self, &_global_svn_swig_rb_pool, &$1);
  _global_pool = $1;
  svn_swig_rb_push_pool(_global_svn_swig_rb_pool);
}

%typemap(ruby, default) (svn_client_ctx_t *ctx, apr_pool_t *pool)
     (VALUE _global_svn_swig_rb_pool, apr_pool_t *_global_pool)
{
  int adjusted_argc = argc;
  VALUE *adjusted_argv = argv;

  svn_swig_rb_adjust_arg_for_client_ctx_and_pool(&adjusted_argc,
                                                 &adjusted_argv);
  svn_swig_rb_get_pool(adjusted_argc, adjusted_argv, self,
                       &_global_svn_swig_rb_pool, &$2);
  _global_pool = $2;
  svn_swig_rb_push_pool(_global_svn_swig_rb_pool);
}

%typemap(ruby, argout) apr_pool_t *pool
{
  svn_swig_rb_set_pool($result, _global_svn_swig_rb_pool);
  svn_swig_rb_pop_pool(_global_svn_swig_rb_pool);
}

#ifdef SWIGPERL
%apply apr_pool_t *pool {
    apr_pool_t *dir_pool,
    apr_pool_t *file_pool,
    apr_pool_t *node_pool
};
#endif

#ifdef SWIGRUBY
%apply apr_pool_t *pool {
    apr_pool_t *dir_pool,
    apr_pool_t *file_pool,
    apr_pool_t *node_pool
};
#endif


/* -----------------------------------------------------------------------
   CALLBACK_BATON: Do not convert to C object from Ruby object.
*/

%typemap(ruby, in) void *CALLBACK_BATON
{
  $1 = (void *)$input;
}


/* -----------------------------------------------------------------------
   Callback: svn_log_message_receiver_t
   svn_client_log()
   svn_ra get_log()
   svn_repos_get_logs()
*/

%typemap(python, in) (svn_log_message_receiver_t receiver, 
                      void *receiver_baton) {
    $1 = svn_swig_py_log_receiver;
    $2 = (void *)$input;
}
%typemap(perl5, in) (svn_log_message_receiver_t receiver, 
                     void *receiver_baton) {
    $1 = svn_swig_pl_thunk_log_receiver;
    $2 = (void *)$input;
}

%typemap(ruby, in) (svn_log_message_receiver_t receiver, 
                    void *receiver_baton) {
    $1 = svn_swig_rb_log_receiver;
    $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

/* -----------------------------------------------------------------------
   Callback: svn_commit_callback_t
   svn_ra get_commit_editor()
   svn_repos_get_commit_editor()
*/

%typemap(perl5, in) (svn_commit_callback_t callback, void *callback_baton) {
    $1 = svn_swig_pl_thunk_commit_callback;
    $2 = (void *)$input;
    svn_swig_pl_hold_ref_in_pool (_global_pool, $input);
};

%typemap(ruby, in) (svn_commit_callback_t callback, void *callback_baton)
{
  $1 = svn_swig_rb_commit_callback;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
};

%typemap(ruby, argout) (svn_commit_callback_t callback, void *callback_baton)
{
  svn_swig_rb_set_baton($result, (VALUE)$2);
};

/* -----------------------------------------------------------------------
   Callback: svn_commit_callback2_t
   svn_ra get_commit_editor2()
   svn_repos_get_commit_editor4()
*/

%typemap(ruby, in) (svn_commit_callback2_t callback, void *callback_baton)
{
  $1 = svn_swig_rb_commit_callback2;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
};

%typemap(ruby, argout) (svn_commit_callback2_t callback, void *callback_baton)
{
  svn_swig_rb_set_baton($result, (VALUE)$2);
};

/* -----------------------------------------------------------------------
   Callback: svn_cancel_func_t
*/

%typemap(python, in) (svn_cancel_func_t cancel_func, void *cancel_baton) {
  $1 = svn_swig_py_cancel_func;
  $2 = $input; /* our function is the baton. */
}

%typemap(ruby, in) (svn_cancel_func_t cancel_func, void *cancel_baton)
{
  $1 = svn_swig_rb_cancel_func;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

%typemap(ruby, argout) (svn_cancel_func_t cancel_func, void *cancel_baton)
{
  svn_swig_rb_set_baton($result, (VALUE)$2);
};


/* -----------------------------------------------------------------------
   Callback: svn_info_receiver_t
*/

%typemap(perl5, in) (svn_info_receiver_t receiver, void *receiver_baton)
{
  $1 = svn_swig_pl_info_receiver;
  $2 = (void *)$input;
}

%typemap(ruby, in) (svn_info_receiver_t receiver, void *receiver_baton)
{
  $1 = svn_swig_rb_info_receiver;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

/* -----------------------------------------------------------------------
   Callback: svn_fs_warning_callback_t
*/

%typemap(ruby, in) (svn_fs_warning_callback_t warning, void *warning_baton)
{
  VALUE baton = svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
  svn_swig_rb_fs_warning_callback_baton_register(baton, _global_pool);
  $1 = svn_swig_rb_fs_warning_callback;
  $2 = (void *)baton;
}

/* -----------------------------------------------------------------------
   svn_stream_t interoperability with language native io handles
*/

%typemap(python, in) svn_stream_t *WRAPPED_STREAM {
    $1 = svn_swig_py_make_stream ($input, _global_pool);
}

%typemap(perl5, in) svn_stream_t * {
    svn_swig_pl_make_stream (&$1, $input);
}

%typemap(perl5, out) svn_stream_t * {
    $result = svn_swig_pl_from_stream ($1);
    argvi++;
}

%typemap(perl5, argout) svn_stream_t ** {
    $result = svn_swig_pl_from_stream (*$1);
    argvi++;
}

%typemap(ruby, in) svn_stream_t * {
  $1 = svn_swig_rb_make_stream($input);
}

/* -----------------------------------------------------------------------
   Wrap the digest output for functions populating digests.
*/

%typemap(in, numinputs=0) unsigned char digest[ANY]
    ($*1_type temp[APR_MD5_DIGESTSIZE]) {
    $1 = ($1_ltype)temp;
}

%typemap(python, argout, fragment="t_output_helper") unsigned char digest[ANY]
{
    $result = t_output_helper($result,
        PyString_FromString(svn_md5_digest_to_cstring ($1, _global_pool)));
}

%typemap(perl5, argout) unsigned char digest[ANY] {
    ST(argvi) = sv_newmortal();
    sv_setpv((SV*)ST(argvi++), svn_md5_digest_to_cstring ($1,_global_pool));
}

%typemap(ruby, argout, fragment="output_helper") unsigned char digest[ANY]
{
  char *digest_string = (char *)svn_md5_digest_to_cstring($1, _global_pool);
  $result = output_helper($result, rb_str_new2(digest_string));
}


%typemap(ruby, in, numinputs=0) unsigned char **digest ($*1_type temp)
{
    $1 = &temp;
}

%typemap(ruby, argout, fragment="output_helper") unsigned char **digest
{
  char *digest_string = (char *)svn_md5_digest_to_cstring(*$1, _global_pool);
  $result = output_helper($result, rb_str_new2(digest_string));
}

/* svn_md5_* functions takes const ones as input */
%typemap(perl5, in) const unsigned char digest[] {
    SWIG_ConvertPtr($input, (void **)&$1, $1_descriptor, 0);
}
%typemap(perl5, argout) const unsigned char digest[] {}

%typemap(ruby, in) const unsigned char *digest
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else if (RSTRING($input)->len != APR_MD5_DIGESTSIZE) {
    rb_raise(rb_eArgError, "digest size (%d) must be %d",
             RSTRING($input)->len, APR_MD5_DIGESTSIZE);
  } else {
    $1 = ($1_ltype)StringValuePtr($input);
  }
}
%typemap(ruby, argout) const unsigned char *digest {}

/*
   svn_txdelta_send_stream() uses *digest not digest[] .
   svn_txdelta_apply() uses *result_digest not digest[] .
*/

%typemap(in, numinputs=0) unsigned char *result_digest {
    $1 = (unsigned char *)apr_palloc(_global_pool, APR_MD5_DIGESTSIZE);
}

%typemap(perl5, argout) unsigned char *result_digest {
    ST(argvi) = sv_newmortal();
    ST(argvi++) = svn_swig_pl_from_md5($1);
}

%apply unsigned char digest[ANY] {
    unsigned char *digest
};

#ifndef SWIGPERL
%apply unsigned char digest[ANY] {
    unsigned char *result_digest
};
#endif

/* -----------------------------------------------------------------------
  useful convertors for svn_opt_revision_t
*/
%typemap(perl5, in) svn_opt_revision_t * (svn_opt_revision_t rev) {
    $1 = &rev;
    if ($input == NULL || $input == &PL_sv_undef || !SvOK($input)) {
        rev.kind = svn_opt_revision_unspecified;
    }
    else if (sv_isobject($input) && sv_derived_from($input, "_p_svn_opt_revision_t")) {
        SWIG_ConvertPtr($input, (void **)&$1, $1_descriptor, 0);
    }
    else if (looks_like_number($input)) {
        rev.kind = svn_opt_revision_number;
        rev.value.number = SvIV($input);
    }
    else if (SvPOK($input)) {
        char *input = SvPV_nolen($input);
        if (strcasecmp(input, "BASE") == 0)
            rev.kind = svn_opt_revision_base;
        else if (strcasecmp(input, "HEAD") == 0)
            rev.kind = svn_opt_revision_head;
        else if (strcasecmp(input, "WORKING") == 0)
            rev.kind = svn_opt_revision_working;
        else if (strcasecmp(input, "COMMITTED") == 0)
            rev.kind = svn_opt_revision_committed;
        else if (strcasecmp(input, "PREV") == 0)
            rev.kind = svn_opt_revision_previous;
        else if (*input == '{') {
            svn_boolean_t matched;
            apr_time_t tm;
            svn_error_t *err;

            char *end = strchr(input,'}');
            if (!end)
                SWIG_croak("unknown opt_revision_t type");
            *end = '\0';
            err = svn_parse_date (&matched, &tm, input + 1, apr_time_now(),
                                  svn_swig_pl_make_pool ((SV *)NULL));
            if (err) {
                svn_error_clear (err);
                SWIG_croak("unknown opt_revision_t type");
            }
            if (!matched)
                SWIG_croak("unknown opt_revision_t type");

            rev.kind = svn_opt_revision_date;
            rev.value.date = tm;
        } else
            SWIG_croak("unknown opt_revision_t type");
    } else
        SWIG_croak("unknown opt_revision_t type");
}

%typemap(ruby, in) svn_opt_revision_t * (svn_opt_revision_t rev) {
  $1 = &rev;
  svn_swig_rb_set_revision(&rev, $input);
}

/* -----------------------------------------------------------------------
   apr_hash_t **dirents
   svn_client_ls()
   svn_io_get_dirents()
   svn_ra get_dir()
*/

%typemap(python,in,numinputs=0) apr_hash_t **dirents = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **dirents {
  $result = t_output_helper($result, svn_swig_py_convert_hash(*$1, 
    $descriptor(svn_dirent_t *), _global_svn_swig_py_pool));
}

%typemap(perl5,in,numinputs=0) apr_hash_t **dirents = apr_hash_t **OUTPUT;
%typemap(perl5,argout) apr_hash_t **dirents {
    ST(argvi++) = svn_swig_pl_convert_hash
	(*$1, $descriptor(svn_dirent_t *));
}

%typemap(ruby, in, numinputs=0) apr_hash_t **dirents = apr_hash_t **OUTPUT;
%typemap(ruby, argout, fragment="output_helper") apr_hash_t **dirents
{
  $result = output_helper($result,
                          svn_swig_rb_apr_hash_to_hash_swig_type
                            (*$1, "svn_dirent_t *"));
}

/* -----------------------------------------------------------------------
   apr_hash_t **locks
   svn_client_ls3()
   svn_repos_fs_get_locks()
   svn_ra_get_locks()
*/

%typemap(ruby, in, numinputs=0) apr_hash_t **locks = apr_hash_t **OUTPUT;
%typemap(ruby, argout, fragment="output_helper") apr_hash_t **locks
{
  $result = output_helper($result,
                          svn_swig_rb_apr_hash_to_hash_swig_type
                            (*$1, "svn_lock_t *"));
}

/* -----------------------------------------------------------------------
   apr_array_header_t *location_revisions
   svn_repos_trace_node_locations()
   svn_ra_get_locations()
*/

%typemap(ruby, in) apr_array_header_t *location_revisions
{
  $1 = svn_swig_rb_array_to_apr_array_revnum($input, _global_pool);
}

/* -----------------------------------------------------------------------
   apr_hash_t **locations
   svn_repos_trace_node_locations()
   svn_ra_get_locations()
*/

%typemap(ruby, in, numinputs=0) apr_hash_t **locations = apr_hash_t **OUTPUT;
%typemap(ruby, argout) apr_hash_t **locations
{
  $result = svn_swig_rb_apr_revnum_key_hash_to_hash_string(*$1);
}

/* -----------------------------------------------------------------------
   Special boolean mapping for ruby.
*/

%typemap(ruby, in) svn_boolean_t "$1 = RTEST($input);";
%typemap(ruby, out, fragment="output_helper") svn_boolean_t
{
  $result = output_helper($result, $1 ? Qtrue : Qfalse);
}

%typemap(ruby, in, numinputs=0) svn_boolean_t * (svn_boolean_t temp)
{
  $1 = &temp;
}

%typemap(ruby, argout, fragment="output_helper") svn_boolean_t *
{
  $result = output_helper($result, *$1 ? Qtrue : Qfalse);
}

/* -----------------------------------------------------------------------
   Handle python thread locking.

   Swig doesn't allow us to specify a language in the %exception command,
   so we have to use #ifdefs for the python-specific parts.
*/

%exception {
#ifdef SWIGPYTHON
    svn_swig_py_release_py_lock();
#endif
    $action
#ifdef SWIGPYTHON
    svn_swig_py_acquire_py_lock();
#endif
}


/* -----------------------------------------------------------------------
   handle config and fs_config in svn_{fs,repos}_create
*/


%typemap(ruby, in) apr_hash_t *config
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    $1 = svn_swig_rb_hash_to_apr_hash_swig_type($input, "svn_config_t *", _global_pool);
  }
}
%typemap(ruby, in) apr_hash_t *fs_config
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    $1 = svn_swig_rb_hash_to_apr_hash_string($input, _global_pool);
  }
}

/* ----------------------------------------------------------------------- */

%{
#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGPERL
#include "swigutil_pl.h"
#endif

#ifdef SWIGRUBY
#include <apu.h>
#include <apr_xlate.h>
#include "swigutil_rb.h"
#endif
%}

%include svn_time_h.swg
%include svn_types_h.swg
