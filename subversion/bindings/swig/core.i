/*
 * core.i :  SWIG interface file for various core SVN and APR components
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

#ifdef SWIGPERL
%module "SVN::_Core"
#else
%module core
#endif

%include typemaps.i

%{
#include "svn_opt.h"
%}

/* We don't want to hear about supposedly bad constant values */
#pragma SWIG nowarn=305

/* ### for now, let's ignore this thing. */
%ignore svn_prop_t;

/* -----------------------------------------------------------------------
   The following struct members have to be read-only because otherwise
   strings assigned to then would never be freed, resulting in memory
   leaks. This prevents the swig warning "Warning(451): Setting const
   char * member may leak memory."
*/
%immutable svn_log_changed_path_t::copyfrom_path;
%immutable svn_dirent_t::last_author;
%immutable svn_error_t::message;
%immutable svn_error_t::file;

/* ----------------------------------------------------------------------- 
   We want the error code enums wrapped so we must include svn_error_codes.h
   before anything else does. 
*/

%include svn_error_codes.h

/* ----------------------------------------------------------------------- 
   Include svn_types.h early. other .i files will import svn_types.i which
   then includes svn_types.h, making further includes get skipped. we want
   to actually generate wrappers, so manage svn_types.h right here.
*/

%include svn_types.h


/* ----------------------------------------------------------------------- 
   moving along...
*/
%import apr.i
%import svn_types.i
%import svn_string.i

/* ----------------------------------------------------------------------- 
   completely ignore a number of functions. the presumption is that the
   scripting language already has facilities for these things (or they
   are relatively trivial).
*/
%ignore svn_io_check_path;
%ignore svn_io_check_resolved_path;
%ignore svn_io_temp_dir;
%ignore svn_io_copy_file;
%ignore svn_io_copy_dir_recursively;
%ignore svn_io_make_dir_recursively;
%ignore svn_io_dir_empty;
%ignore svn_io_append_file;
%ignore svn_io_set_file_read_only;
%ignore svn_io_set_file_read_write;
%ignore svn_io_set_file_executable;
%ignore svn_io_is_file_executable;
%ignore svn_io_read_length_line;
%ignore svn_io_file_affected_time;
%ignore svn_io_set_file_affected_time;
%ignore svn_io_filesizes_different_p;
%ignore svn_io_file_create;
%ignore svn_io_file_lock;
%ignore svn_io_dir_file_copy;
%ignore svn_io_remove_file;
%ignore svn_io_remove_dir;
%ignore svn_io_get_dirents;
%ignore svn_io_dir_walk;
%ignore svn_io_run_cmd;
%ignore svn_io_run_diff;
%ignore svn_io_run_diff3;
%ignore svn_io_file_open;
%ignore svn_io_file_close;
%ignore svn_io_file_getc;
%ignore svn_io_file_info_get;
%ignore svn_io_file_read;
%ignore svn_io_file_read_full;
%ignore svn_io_file_seek;
%ignore svn_io_file_write;
%ignore svn_io_file_write_full;
%ignore svn_io_stat;
%ignore svn_io_file_rename;
%ignore svn_io_dir_make;
%ignore svn_io_dir_make_hidden;
%ignore svn_io_dir_open;
%ignore svn_io_dir_remove_nonrecursive;
%ignore svn_io_dir_read;
%ignore svn_io_read_version_file;
%ignore svn_io_write_version_file;

%ignore apr_check_dir_empty;

/* bad pool convention */
%ignore svn_opt_print_generic_help;

/* scripts can do the printf, then write to a stream. we can't really
   handle the variadic, so ignore it. */
%ignore svn_stream_printf;

/* Ugliness because the constants are typedefed and SWIG ignores them
   as a result. */
%constant svn_revnum_t SWIG_SVN_INVALID_REVNUM = -1;
%constant svn_revnum_t SWIG_SVN_IGNORED_REVNUM = -1;

/* -----------------------------------------------------------------------
   these types (as 'type **') will always be an OUT param
*/
%apply SWIGTYPE **OUTPARAM {
  svn_auth_baton_t **, svn_diff_t **
}

/* -----------------------------------------------------------------------
   apr_size_t * is always an IN/OUT parameter in svn_io.h
*/
%apply apr_size_t *INOUT { apr_size_t * };

/* -----------------------------------------------------------------------
   handle the MIME type return value of svn_io_detect_mimetype()
*/
%apply const char **OUTPUT { const char ** };

/* -----------------------------------------------------------------------
   handle the providers array as an input type.
*/
%typemap(python, in) apr_array_header_t *providers {
    svn_auth_provider_object_t *provider;
    int targlen;
    if (!PySequence_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "not a sequence");
        return NULL;
    }
    targlen = PySequence_Length($input);
    $1 = apr_array_make(_global_pool, targlen, sizeof(provider));
    ($1)->nelts = targlen;
    while (targlen--) {
        SWIG_ConvertPtr(PySequence_GetItem($input, targlen),
                        (void **)&provider, 
                        $descriptor(svn_auth_provider_object_t *),
                        SWIG_POINTER_EXCEPTION | 0);
        APR_ARRAY_IDX($1, targlen, svn_auth_provider_object_t *) = provider;
    }
}

/* -----------------------------------------------------------------------
   fix up the svn_stream_read() ptr/len arguments
*/
%typemap(python, in) (char *buffer, apr_size_t *len) ($*2_type temp) {
    if (!PyInt_Check($input)) {
        PyErr_SetString(PyExc_TypeError,
                        "expecting an integer for the buffer size");
        return NULL;
    }
    temp = PyInt_AsLong($input);
    if (temp < 0) {
        PyErr_SetString(PyExc_ValueError,
                        "buffer size must be a positive integer");
        return NULL;
    }
    $1 = malloc(temp);
    $2 = ($2_ltype)&temp;
}
%typemap(perl5, in) (char *buffer, apr_size_t *len) ($*2_type temp) {
    temp = SvIV($input);
    $1 = malloc(temp);
    $2 = ($2_ltype)&temp;
}

/* ### need to use freearg or somesuch to ensure the string is freed.
   ### watch out for 'return' anywhere in the binding code. */

%typemap(python, argout, fragment="t_output_helper") (char *buffer, apr_size_t *len) {
    $result = t_output_helper($result, PyString_FromStringAndSize($1, *$2));
    free($1);
}
%typemap(perl5, argout) (char *buffer, apr_size_t *len) {
    $result = sv_newmortal();
    sv_setpvn ($result, $1, *$2);
    free($1);
    argvi++;
}

/* -----------------------------------------------------------------------
   fix up the svn_stream_write() ptr/len arguments
*/
%typemap(python, in) (const char *data, apr_size_t *len) ($*2_type temp) {
    if (!PyString_Check($input)) {
        PyErr_SetString(PyExc_TypeError,
                        "expecting a string for the buffer");
        return NULL;
    }
    $1 = PyString_AS_STRING($input);
    temp = PyString_GET_SIZE($input);
    $2 = ($2_ltype)&temp;
}
%typemap(perl5, in) (const char *data, apr_size_t *len) ($*2_type temp) {
    $1 = SvPV($input, temp);
    $2 = ($2_ltype)&temp;
}

%typemap(python, argout, fragment="t_output_helper") (const char *data, apr_size_t *len) {
    $result = t_output_helper($result, PyInt_FromLong(*$2));
}

%typemap(perl5, argout, fragment="t_output_helper") (const char *data, apr_size_t *len) {
    $result = sv_2mortal (newSViv(*$2));
}

/* auth provider convertors */

%typemap(perl5, in) apr_array_header_t *providers {
    $1 = (apr_array_header_t *) svn_swig_pl_objs_to_array($input, SWIGTYPE_p_svn_auth_provider_object_t, _global_pool);
}


/* -----------------------------------------------------------------------
   describe how to pass a FILE* as a parameter (svn_stream_from_stdio)
*/
%typemap(python, in) FILE * {
    $1 = PyFile_AsFile($input);
    if ($1 == NULL) {
        PyErr_SetString(PyExc_ValueError, "Must pass in a valid file object");
        return NULL;
    }
}
%typemap(perl5, in) FILE * {
    $1 = PerlIO_exportFILE (IoIFP (sv_2io ($input)), NULL);
}

/* -----------------------------------------------------------------------
   wrap some specific APR functionality
*/

apr_status_t apr_initialize(void);
void apr_terminate(void);

apr_status_t apr_time_ansi_put(apr_time_t *result, time_t input);

void apr_pool_destroy(apr_pool_t *p);
void apr_pool_clear(apr_pool_t *p);

apr_status_t apr_file_open_stdout (apr_file_t **out, apr_pool_t *pool);
apr_status_t apr_file_open_stderr (apr_file_t **out, apr_pool_t *pool);

/* -----------------------------------------------------------------------
   pool functions renaming since swig doesn't take care of the #define's
*/
%rename (svn_pool_create) svn_pool_create_ex;
%ignore svn_pool_create_ex_debug;
%typemap(default) apr_allocator_t *allocator {
    $1 = NULL;
}

/* -----------------------------------------------------------------------
   Default pool handling for perl.
*/
#ifdef SWIGPERL

%typemap (varin) apr_pool_t *current_pool {
        $1_ltype argp;
        if (SWIG_ConvertPtr($input, (void **) &argp, $1_descriptor,0) < 0) {
            croak("Type error in assignment to $symname. Expected $1_mangle");
        }
        svn_swig_pl_set_current_pool(argp);
}

%typemap (varout,type="$1_descriptor") apr_pool_t *current_pool
  "sv_setiv(SvRV($result),(IV) svn_swig_pl_get_current_pool());";

apr_pool_t *current_pool;

#endif

/* -----------------------------------------------------------------------
   wrap config functions
*/

%typemap(perl5,in,numinputs=0) apr_hash_t **cfg_hash = apr_hash_t **OUTPUT;
%typemap(perl5,argout) apr_hash_t **cfg_hash {
    ST(argvi++) = svn_swig_pl_convert_hash(*$1, SWIGTYPE_p_svn_config_t);
}

%typemap(perl5, in) (svn_config_enumerator_t callback, void *baton) {
    $1 = svn_swig_pl_thunk_config_enumerator,
    $2 = (void *)$input;
};

%typemap(python,in,numinputs=0) apr_hash_t **cfg_hash = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **cfg_hash {
    $result = t_output_helper(
        $result,
        SWIG_NewPointerObj(*$1, SWIGTYPE_p_apr_hash_t, 0));
}

/* Allow None to be passed as config_dir argument */
%typemap(python,in,parse="z") const char *config_dir "";

#ifdef SWIGPYTHON
PyObject *svn_swig_py_exception_type(void);
#endif


/* ----------------------------------------------------------------------- */

%include svn_types.h
%include svn_pools.h
%include svn_version.h
%include svn_time.h
%include svn_props.h
%include svn_opt.h
%include svn_auth.h
%include svn_config.h
%include svn_version.h


/* SWIG won't follow through to APR's defining this to be empty, so we
   need to do it manually, before SWIG sees this in svn_io.h. */
#define __attribute__(x)

%include svn_io.h

#ifdef SWIGPERL
%include svn_diff.h
%include svn_error.h
#endif

%{
#include <apr.h>
#include <apr_general.h>

#include "svn_io.h"
#include "svn_pools.h"
#include "svn_version.h"
#include "svn_time.h"
#include "svn_props.h"
#include "svn_opt.h"
#include "svn_auth.h"
#include "svn_config.h"
#include "svn_version.h"
#include "svn_md5.h"
#include "svn_diff.h"
#include "svn_error_codes.h"

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGJAVA
#include "swigutil_java.h"
#endif

#ifdef SWIGPERL
#include "swigutil_pl.h"
#endif
%}

#ifdef SWIGPYTHON
%init %{
/* This is a hack.  I dunno if we can count on SWIG calling the module "m" */
PyModule_AddObject(m, "SubversionException", 
                   svn_swig_py_register_exception());
%}

%pythoncode %{
SubversionException = _core.SubversionException
%}
#endif
