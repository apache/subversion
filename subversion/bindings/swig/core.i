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

%module core

%include typemaps.i

%{
#include "svn_opt.h"
%}

/* ----------------------------------------------------------------------- 
   include svn_types.h early. other .i files will import svn_types.i which
   then includes svn_types.h, making further includes get skipped. we want
   to actually generate wrappers, so manage svn_types.h right here.
*/

/* ### for now, let's ignore this thing. */
%ignore svn_prop_t;

/* -----------------------------------------------------------------------
   The following struct members have to be read-only because otherwise
   strings assigned to then would never be freed, resulting in memory
   leaks. This prevents the swig warning "Warning(451): Setting const
   char * member may leak memory."
*/
%immutable svn_log_changed_path_t::copyfrom_path;
%immutable svn_dirent::last_author;
%immutable svn_error::message;
%immutable svn_error::file;

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
%ignore svn_io_copy_file;
%ignore svn_io_copy_dir_recursively;
%ignore svn_io_append_file;
%ignore svn_io_read_length_line;
%ignore svn_io_file_affected_time;
%ignore svn_io_fd_from_file;
%ignore svn_io_get_dirents;
%ignore svn_io_run_cmd;
%ignore svn_io_remove_file;
%ignore svn_io_remove_dir;
%ignore svn_io_make_dir_recursively;
%ignore svn_io_set_file_read_only;
%ignore svn_io_set_file_read_write;
%ignore svn_io_set_file_executable;
%ignore svn_io_filesizes_different_p;
%ignore svn_io_file_printf;

%ignore apr_check_dir_empty;

/* bad pool convention */
%ignore svn_opt_print_generic_help;

/* scripts can do the printf, then write to a stream. we can't really
   handle the variadic, so ignore it. */
%ignore svn_stream_printf;


/* -----------------------------------------------------------------------
   these types (as 'type **') will always be an OUT param
*/
%apply SWIGTYPE **OUTPARAM {
  svn_auth_baton_t **
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
    $result = newSViv(*$2);

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
    dSP ;
    int count, fd ;

    ENTER ;
    SAVETMPS;

    PUSHMARK(SP) ;
    XPUSHs($input);
    PUTBACK ;

    count = call_pv("fileno", G_SCALAR);
    SPAGAIN ;

    if (count != 1)
        croak("Big trouble\n") ;

    if (fd = POPi < 0)
        croak("not an accessible filehandle");

    $1 = fdopen (fd, "r+");

    PUTBACK ;
    FREETMPS ;
    LEAVE ;
}

/* -----------------------------------------------------------------------
   the second argument to svn_parse_date is unused: always pass NULL
*/

%typemap(python,in,numinputs=0) struct getdate_time *now {
    $1 = NULL;
}

/* ignore the related structure */
/* ### hmm... this structure isn't namespace protected?! */
%ignore getdate_time;

/* -----------------------------------------------------------------------
   wrap some specific APR functionality
*/

apr_status_t apr_initialize(void);
void apr_terminate(void);

apr_status_t apr_time_ansi_put(apr_time_t *result, time_t input);

void apr_pool_destroy(apr_pool_t *p);
void apr_pool_clear(apr_pool_t *p);

apr_status_t *apr_file_open_stdout (apr_file_t **out, apr_pool_t *pool);
apr_status_t *apr_file_open_stderr (apr_file_t **out, apr_pool_t *pool);

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
apr_pool_t *current_pool;
#endif

/* -----------------------------------------------------------------------
   wrap config functions
*/

%typemap(perl5,in,numinputs=0) apr_hash_t **cfg_hash = apr_hash_t **OUTPUT;
%typemap(perl5,argout) apr_hash_t **cfg_hash {
    ST(argvi++) = svn_swig_pl_convert_hash(*$1, SWIGTYPE_p_svn_config_t);
}

%typemap(python,in,numinputs=0) apr_hash_t **cfg_hash = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **cfg_hash {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, SWIGTYPE_p_svn_config_t));
}

/* Allow None to be passed as config_dir argument */
%typemap(python,in,parse="z") const char *config_dir "";


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
