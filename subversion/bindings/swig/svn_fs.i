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
    svn_stream_t **
};

/* and this is always an OUT param */
%apply const char **OUTPUT { const char ** };

/* ### need to deal with IN params which have "const" and OUT params which
   ### return non-const type. SWIG's type checking may see these as
   ### incompatible. */

/* -----------------------------------------------------------------------
   These parameters may be NULL.
*/
%apply const char *MAY_BE_NULL {
    const char *base_checksum,
    const char *result_checksum
};

/* -----------------------------------------------------------------------
   for the FS, 'int *' will always be an OUTPUT parameter
*/
%apply int *OUTPUT { int * };

%apply enum SWIGTYPE *OUTENUM { svn_node_kind_t * };

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

%typemap(perl5, argout) apr_array_header_t **names_p {
    /* ### FIXME-perl */
}
/* -----------------------------------------------------------------------
   revisions_changed's "apr_array_header_t **" is returning a list of
   revs.  also, its input array is a list of strings.
*/

%typemap(python, argout, fragment="t_output_helper") 
apr_array_header_t **revs {
    $result = t_output_helper($result, svn_swig_py_revarray_to_list(*$1));
}
%typemap(perl5, argout) apr_array_header_t **revs {
    /* ### FIXME-perl */
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

%typemap(python,in,numinputs=0) apr_hash_t **entries_p = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **entries_p {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, SWIGTYPE_p_svn_fs_dirent_t));
}
%typemap(perl5,argout) apr_hash_t **entries_p {
    /* ### FIXME-perl */
}

/* -----------------------------------------------------------------------
   and except for svn_fs_paths_changed, which returns svn_fs_path_change_t
   structures
*/

%typemap(python, in,numinputs=0) apr_hash_t **changed_paths_p = apr_hash_t **OUTPUT;
%typemap(python, argout, fragment="t_output_helper") apr_hash_t **changed_paths_p {
    $result = t_output_helper(
        $result,
        svn_swig_py_convert_hash(*$1, SWIGTYPE_p_svn_fs_path_change_t));
}

/* -----------------------------------------------------------------------
   Fix the return value for svn_fs_commit_txn(). If the conflict result is
   NULL, then t_output_helper() is passed Py_None, but that goofs up
   because that is *also* the marker for "I haven't started assembling a
   multi-valued return yet" which means the second return value (new_rev)
   will not cause a 2-tuple to be manufactured.

   The answer is to explicitly create a 2-tuple return value.
*/
%typemap(python, argout) (const char **conflict_p, svn_revnum_t *new_rev) {
    /* this is always Py_None */
    Py_DECREF($result);
    /* build the result tuple */
    $result = Py_BuildValue("zi", *$1, (long)*$2);
}

%typemap(perl5, argout) apr_hash_t **changed_paths_p {
    /* ### FIXME-perl */
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
