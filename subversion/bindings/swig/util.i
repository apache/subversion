/*
 * util.i :  SWIG interface file for various SVN and APR utilities
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

%module _util

%include typemaps.i

%{
#include "svn_opt.h"
%}

/* ----------------------------------------------------------------------- 
   include svn_types.h early. other .i files will import svn_types.i which
   then includes svn_types.h, making further includes get skipped. we want
   to actually generate wrappers, so manage svn_types.h right here.
*/
%ignore svn_error;

/* ### for now, let's not try to handle these structures. swig complains
   ### about setting the 'const char *' inside the struct might leak mem  */
%ignore svn_log_changed_path_t;

/* ### We also get complaints about possible memory leakage for svn_dirent,
   ### but we can live with it for now. */
/* %ignore svn_dirent; */

/* ### for now, let's ignore this thing. */
%ignore svn_prop_t;

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

/* scripts can do the printf, then write to a stream. we can't really
   handle the variadic, so ignore it. */
%ignore svn_stream_printf;

/* -----------------------------------------------------------------------
   apr_size_t * is always an IN/OUT parameter in svn_io.h
*/
%apply apr_size_t *INOUT { apr_size_t * };

/* -----------------------------------------------------------------------
   handle the MIME type return value of svn_io_detect_mimetype()
*/
%apply const char **OUTPUT { const char ** };

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

/* ### need to use freearg or somesuch to ensure the string is freed.
   ### watch out for 'return' anywhere in the binding code. */

%typemap(python, argout, fragment="t_output_helper") (char *buffer, apr_size_t *len) {
    $result = t_output_helper($result, PyString_FromStringAndSize($1, *$2));
    free($1);
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

%typemap(python, argout, fragment="t_output_helper") (const char *data, apr_size_t *len) {
    $result = t_output_helper($result, PyInt_FromLong(*$2));
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

/* -----------------------------------------------------------------------
   the second argument to svn_parse_date is unused: always pass NULL
*/

%typemap(in,numinputs=0) struct getdate_time *now {
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

/* ----------------------------------------------------------------------- */

%include svn_types.h
%include svn_pools.h
%include svn_version.h
%include svn_time.h
%include svn_props.h
%include svn_opt.h

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

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGJAVA
#include "swigutil_java.h"
#endif
%}
