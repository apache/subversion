/*
 * svn_repos.i :  SWIG interface file for svn_repos.h
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

%module repos
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
   Some of the various parameters need to be NULL sometimes
*/
%apply const char *MAY_BE_NULL {
    const char *src_entry,
    const char *on_disk_template,
    const char *in_repos_template
};

/* -----------------------------------------------------------------------
   handle the 'paths' parameter appropriately
*/
%apply const apr_array_header_t *STRINGLIST {
    const apr_array_header_t *paths
};

/* -----------------------------------------------------------------------
   commit editor support	
*/
%apply SWIGTYPE **OUTPARAM {
    const svn_delta_editor_t **editor,
    void **edit_baton
};

%typemap(perl5, in) (svn_repos_commit_callback_t hook, void *callback_baton) {
    $1 = svn_swig_pl_thunk_commit_callback;
    $2 = (void *)$input;
    SvREFCNT_inc($input);
};

/* ----------------------------------------------------------------------- */

%include svn_repos.h
%{
#include "svn_repos.h"

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
