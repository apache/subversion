/*
 * svn_repos.i :  SWIG interface file for svn_repos.h
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

%module _repos
%include typemaps.i

%import apr.i
%import svn_types.i
%import svn_string.i
%import svn_fs.i

/* -----------------------------------------------------------------------
   these types (as 'type **') will always be an OUT param
*/
%apply SWIGTYPE **OUTPARAM {
    svn_repos_t **
};

/* -----------------------------------------------------------------------
   dir_delta's src_entry parameter needs to be NULL sometimes
*/
%typemap(python,in,parse="z") const char *src_entry "";

/* -----------------------------------------------------------------------
   get_logs takes a callback function, so we have to thunk it
*/
%ignore svn_repos_get_logs;
%rename(get_logs) svn_swig_py_repos_get_logs;
%typemap(python, in) PyObject *py_receiver {
  $1 = $input;
}

#ifdef SWIGPYTHON
svn_error_t * svn_swig_py_repos_get_logs(svn_repos_t *repos,
                                         const apr_array_header_t *paths,
                                         svn_revnum_t start,
                                         svn_revnum_t end,
                                         svn_boolean_t discover_changed_paths,
                                         svn_boolean_t strict_node_history,
                                         PyObject *py_receiver,
                                         apr_pool_t *pool);
#endif


/* -----------------------------------------------------------------------
   handle the 'paths' parameter appropriately
*/
%apply const apr_array_header_t *STRINGLIST {
    const apr_array_header_t *paths
};


/* ----------------------------------------------------------------------- */

%include svn_repos.h
%{
#include "svn_repos.h"

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif
%}
