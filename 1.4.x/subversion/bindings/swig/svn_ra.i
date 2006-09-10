/*
 * svn_ra.i :  SWIG interface file for svn_ra.h
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

#if defined(SWIGPERL)
%module "SVN::_Ra"
#elif defined(SWIGRUBY)
%module "svn::ext::ra"
#else
%module ra
#endif

%include typemaps.i

%include svn_global.swg
%import apr.swg
%import core.i
%import svn_types.swg
%import svn_string.swg
%import svn_delta.i

/* bad pool convention, also these should not be public interface at all
   as commented by sussman. */
%ignore svn_ra_svn_init;
%ignore svn_ra_local_init;
%ignore svn_ra_dav_init;
%ignore svn_ra_serf_init;

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/
%apply SWIGTYPE **OUTPARAM {
    svn_ra_plugin_t **,
    svn_ra_session_t **,
    const svn_ra_reporter2_t **reporter,
    void **report_baton,
    svn_dirent_t **dirent,
    svn_lock_t **lock
};

%apply apr_hash_t **PROPHASH { apr_hash_t **props };

%apply const char *MAY_BE_NULL {
    const char *comment,
    const char *lock_token
};

%apply apr_hash_t *STRING_TO_STRING {
  apr_hash_t *lock_tokens,
  apr_hash_t *path_tokens
};

#ifdef SWIGPYTHON
%apply svn_stream_t *WRAPPED_STREAM { svn_stream_t * };
#endif

/* -----------------------------------------------------------------------
   handle svn_ra_get_locations()
*/
%typemap(python,in) apr_array_header_t *location_revisions {
    $1 = (apr_array_header_t *) svn_swig_py_revnums_to_array($input, 
                                                             _global_pool);
    if ($1 == NULL)
        SWIG_fail;
}
%typemap(python,in,numinputs=0) apr_hash_t **locations = apr_hash_t **OUTPUT;
%typemap(python,argout,fragment="t_output_helper") apr_hash_t **locations {
    $result = t_output_helper($result, svn_swig_py_locationhash_to_dict(*$1));
}

/* -----------------------------------------------------------------------
   thunk ra_callback
*/
%apply const char **OUTPUT {
    const char **url,
    const char **uuid
};

%apply const apr_array_header_t *STRINGLIST {
    const apr_array_header_t *paths
};

%typemap(perl5, in) (const svn_delta_editor_t *update_editor,
		     void *update_baton) {
    svn_delta_make_editor(&$1, &$2, $input, _global_pool);
}
%typemap(perl5, in) (const svn_delta_editor_t *diff_editor,
		     void *diff_baton) {
    svn_delta_make_editor(&$1, &$2, $input, _global_pool);
}

%apply (const svn_delta_editor_t *EDITOR, void *BATON)
{
  (const svn_delta_editor_t *update_editor, void *update_baton),
  (const svn_delta_editor_t *diff_editor, void *diff_baton)
}

%typemap(perl5, in) (const svn_ra_callbacks_t *callbacks,
		     void *callback_baton) {
    svn_ra_make_callbacks(&$1, &$2, $input, _global_pool);
}

%typemap(ruby, in) (const svn_ra_callbacks2_t *callbacks,
                    void *callback_baton)
{
  svn_swig_rb_setup_ra_callbacks(&$1, &$2, $input, _global_pool);
}

%typemap(perl5, in) apr_hash_t *config {
    $1 = svn_swig_pl_objs_to_hash_by_name ($input, "svn_config_t *",
					   _global_pool);
}

%typemap(ruby, in) (svn_ra_lock_callback_t lock_func, void *lock_baton)
{
  $1 = svn_swig_rb_ra_lock_callback;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

%typemap(ruby, in) (svn_ra_file_rev_handler_t handler, void *handler_baton)
{
  $1 = svn_swig_rb_ra_file_rev_handler;
  $2 = (void *)svn_swig_rb_make_baton($input, _global_svn_swig_rb_pool);
}

%typemap(ruby, in) apr_hash_t *path_revs
{
  $1 = svn_swig_rb_hash_to_apr_hash_revnum($input, _global_pool);
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
#include "swigutil_rb.h"
#endif
%}

%include svn_ra_h.swg

