/*
 * svn_fs.i :  SWIG interface file for svn_fs.h
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

%module _fs
%include typemaps.i

%import apr.i
%import svn_types.i
%import svn_string.i
%import svn_delta.i

/* -----------------------------------------------------------------------
   do not generate any constructors or destructors (of structures) -- all
   structures are going to come /out/ of the FS (so we don't need to
   construct the things) and will live in a pool (so we don't need to
   destroy the things).
*/
%nodefault;

/* -----------------------------------------------------------------------
   these types (as 'type **') will always be an OUT param
*/
%apply SWIGTYPE **OUTPARAM {
    svn_fs_root_t **,
    svn_fs_txn_t **,
    void **,
    svn_fs_id_t **,
    const char **,
    svn_stream_t **
};

/* ### need to deal with IN params which have "const" and OUT params which
   ### return non-const type. SWIG's type checking may see these as
   ### incompatible. */

/* -----------------------------------------------------------------------
   for the FS, 'int *' will always be an OUTPUT parameter
*/
%apply int *OUTPUT { int * };

/* -----------------------------------------------------------------------
   define the data/len pair of svn_fs_parse_id to be a single argument
*/
%apply (const char *PTR, apr_size_t LEN) {
    (const char *data, apr_size_t len)
}

/* -----------------------------------------------------------------------
   list_transaction's "apr_array_header_t **" is returning a list of strings.
*/

%typemap(in,numinputs=0) apr_array_header_t ** (apr_array_header_t *temp) {
    $1 = &temp;
}
%typemap(python, argout, fragment="t_output_helper") 
apr_array_header_t **names_p {
    $result = t_output_helper($result, svn_swig_py_array_to_list(*$1));
}

/* -----------------------------------------------------------------------
   revisions_changed's "apr_array_header_t **" is returning a list of
   revs.  also, its input array is a list of strings.
*/

%typemap(python, argout, fragment="t_output_helper") 
apr_array_header_t **revs {
    $result = t_output_helper($result, svn_swig_py_revarray_to_list(*$1));
}
%apply const apr_array_header_t *STRINGLIST { 
    const apr_array_header_t *paths 
};

/* -----------------------------------------------------------------------
   all uses of "apr_hash_t **" are returning property hashes
*/

%apply apr_hash_t **PROPHASH { apr_hash_t ** };

/* -----------------------------------------------------------------------
   except for svn_fs_dir_entries, which returns svn_fs_dirent_t structures
*/

%typemap(in,numinputs=0) apr_hash_t **entries_p = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **entries_p {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, SWIGTYPE_p_svn_fs_dirent_t));
}

/* ----------------------------------------------------------------------- */


%include svn_fs.h

%header %{
#include "svn_fs.h"

#ifdef SWIGPYTHON
#include "swigutil_py.h"
#endif

#ifdef SWIGJAVA
#include "swigutil_java.h"
#endif
%}
