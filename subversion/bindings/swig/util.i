/*
 * util.i :  SWIG interface file for various SVN and APR utilities
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

%module _util

%include "typemaps.i"

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

%ignore apr_check_dir_empty;
%ignore apr_dir_remove_recursively;

/* -----------------------------------------------------------------------
   apr_size_t * is always an IN/OUT parameter in svn_io.h
*/

%typemap(in) apr_size_t * = apr_size_t *INOUT;
%typemap(argout) apr_size_t * = apr_size_t *INOUT;

/* -----------------------------------------------------------------------
   handle the MIME type return value of svn_io_detect_mimetype()
*/

%typemap(ignore) const char ** = const char **OUTPUT;
%typemap(argout) const char ** = const char **OUTPUT;

/* -----------------------------------------------------------------------
   wrap some specific APR functionality
*/

apr_status_t apr_initialize(void);
void apr_terminate(void);

/* ----------------------------------------------------------------------- */

%include svn_io.h
%include svn_pools.h
%include svn_version.h
%{
#include <apr.h>
#include <apr_general.h>
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_version.h"
#include "swigutil.h"
%}
