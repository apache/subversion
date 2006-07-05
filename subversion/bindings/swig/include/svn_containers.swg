/*
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
 *
 * svn_containers.swg: This is a child file of svn_types.swg, and
 *   should not be included directly.  This file should contain
 *   typemaps that deal container types
 *   - i.e. apr_hash_t, apr_array_header_t .
 */

/* =======================================================================
   %typemap(argout) apr_hash_t **
*/

/* -----------------------------------------------------------------------
   Output of apr_hash_t * <svn_dirent_t *> 
*/

#ifdef SWIGPYTHON
%typemap(argout) apr_hash_t **DIRENTHASH {
  %append_output(svn_swig_py_convert_hash(*$1,
                                          $descriptor(svn_dirent_t *),
                                          NULL));
}
#endif

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **DIRENTHASH {
  %append_output(svn_swig_rb_apr_hash_to_hash_swig_type(*$1,
                                                        "svn_dirent_t *"));
}
#endif

/* -----------------------------------------------------------------------
   Output of apr_hash_t * <const char *, svn_string_t *>
   (commonly used for property name and value pairs)
*/

#ifdef SWIGPYTHON
%typemap(argout) apr_hash_t **PROPHASH {
  %append_output(svn_swig_py_prophash_to_dict(*$1));
}
#endif

#ifdef SWIGPERL
%typemap(argout) apr_hash_t **PROPHASH {
  %append_output(svn_swig_pl_prophash_to_hash(*$1));
}
#endif

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **PROPHASH {
  %append_output(svn_swig_rb_apr_hash_to_hash_svn_string(*$1));
}
#endif

/* -----------------------------------------------------------------------
   apr_hash_t **dirents
   svn_client_ls()
   svn_io_get_dirents()
   svn_ra get_dir()
*/

#ifdef SWIGPYTHON
%typemap(argout) apr_hash_t **dirents {
  %append_output(svn_swig_py_convert_hash(*$1,
                                          $descriptor(svn_dirent_t *),
                                          _global_svn_swig_py_pool));
}
#endif

#ifdef SWIGPERL
%typemap(argout) apr_hash_t **dirents {
  %append_output(svn_swig_pl_convert_hash(*$1, $descriptor(svn_dirent_t *)));
}
#endif

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **dirents {
  %append_output(svn_swig_rb_apr_hash_to_hash_swig_type(*$1,
                                                        "svn_dirent_t *"));
}
#endif

/* -----------------------------------------------------------------------
   apr_hash_t **locks
   svn_client_ls3()
   svn_repos_fs_get_locks()
   svn_ra_get_locks()
*/

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **locks {
  %append_output(svn_swig_rb_apr_hash_to_hash_swig_type(*$1, "svn_lock_t *"));
}
#endif

/* -----------------------------------------------------------------------
   apr_hash_t **locations
   svn_repos_trace_node_locations()
   svn_ra_get_locations()
*/

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **locations
{
  %append_output(svn_swig_rb_apr_revnum_key_hash_to_hash_string(*$1));
}
#endif

/* -----------------------------------------------------------------------
   svn_config_read_auth_data()
*/
#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **hash
{
  if (*$1) {
    %append_output(svn_swig_rb_apr_hash_to_hash_svn_string(*$1));
  } else {
    %append_output(Qnil);
  }
}
#endif

#ifdef SWIGPYTHON
/* FIXME: We are effectively treating this hash as an opaque blob...
 * shouldn't we be converting it to a dict? */
%typemap(argout) apr_hash_t **cfg_hash {
  %append_output(svn_swig_NewPointerObj(*$1, $descriptor(apr_hash_t *),
                                        _global_svn_swig_py_pool));
}
#endif

#ifdef SWIGPERL
%typemap(argout) apr_hash_t **cfg_hash {
  %append_output(svn_swig_pl_convert_hash(*$1, $descriptor(svn_config_t *)));
}
#endif

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **cfg_hash {
  %append_output(svn_swig_rb_apr_hash_to_hash_swig_type(*$1,
                                                        "svn_config_t *"));
}
#endif

/* -----------------------------------------------------------------------
   handle svn_ra_get_locations()
*/
#ifdef SWIGPYTHON
%typemap(argout) apr_hash_t **locations {
  %append_output(svn_swig_py_locationhash_to_dict(*$1));
}
#endif

/* -----------------------------------------------------------------------
   handle svn_repos_fs_revision_proplist().
*/

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **dirents {
  %append_output(svn_swig_rb_apr_hash_to_hash_svn_string(*$1));
}
#endif

/* -----------------------------------------------------------------------
   handle the output from svn_repos_trace_node_locations()
*/
#ifdef SWIGPYTHON
%typemap(argout) apr_hash_t **locations {
   %append_output(svn_swig_py_locationhash_to_dict(*$1));
}
#endif

/* -----------------------------------------------------------------------
   apr_hash_t **externals_old
   apr_hash_t **externals_new
   svn_wc_edited_externals()
*/

#ifdef SWIGRUBY
%typemap(argout) apr_hash_t **externals_old, apr_hash_t **externals_new {
  %append_output(svn_swig_rb_apr_hash_to_hash_string(*$1));
}
#endif


/* =======================================================================
   %typemap(in) apr_hash_t *
*/

/* -----------------------------------------------------------------------
   Input of apr_hash_t * <const char *, svn_string_t *>
   (commonly used for property name and value pairs)
*/

#ifdef SWIGPYTHON
%typemap(in) apr_hash_t *PROPHASH
{
  $1 = svn_swig_py_prophash_from_dict($input, _global_pool);
}
#endif

#ifdef SWIGRUBY
%typemap(in) apr_hash_t *PROPHASH
{
  $1 = svn_swig_rb_hash_to_apr_hash_svn_string($input, _global_pool);
}
#endif

#if defined(SWIGPYTHON) || defined(SWIGRUBY)
%apply apr_hash_t *PROPHASH {
  apr_hash_t *target_props,
  apr_hash_t *source_props
};
#endif

/* -----------------------------------------------------------------------
   Input of scripting hash/dictionary (string => string)
   converted to apr_hash_t (const char * => const char *)
*/
#ifdef SWIGPYTHON
%typemap(in) apr_hash_t *STRING_TO_STRING {
    $1 = svn_swig_py_stringhash_from_dict ($input, _global_pool);
}
#endif
#ifdef SWIGPERL
%typemap(in) apr_hash_t *STRING_TO_STRING {
    $1 = svn_swig_pl_strings_to_hash ($input, _global_pool);
}
#endif
#ifdef SWIGRUBY
%typemap(in) apr_hash_t *STRING_TO_STRING {
    $1 = svn_swig_rb_hash_to_apr_hash_string($input, _global_pool);
}
#endif


/* -----------------------------------------------------------------------
   handle config and fs_config in svn_{fs,repos}_create
*/


#ifdef SWIGRUBY
%typemap(in) apr_hash_t *config
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    $1 = svn_swig_rb_hash_to_apr_hash_swig_type($input, "svn_config_t *", _global_pool);
  }
}
%typemap(in) apr_hash_t *fs_config
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    $1 = svn_swig_rb_hash_to_apr_hash_string($input, _global_pool);
  }
}
#endif

/* -----------------------------------------------------------------------
   svn_config_write_auth_data()
*/
#ifdef SWIGRUBY
%typemap(in) apr_hash_t *hash
{
  $1 = svn_swig_rb_hash_to_apr_hash_svn_string($input, _global_pool);
}
#endif

#ifdef SWIGRUBY
%typemap(in) apr_hash_t *path_revs
{
  $1 = svn_swig_rb_hash_to_apr_hash_revnum($input, _global_pool);
}
#endif

/* =======================================================================
   %typemap(argout) apr_array_header_t **
*/

/* -----------------------------------------------------------------------
   Output of apr_array_header_t * <svn_prop_t *>
*/
#ifdef SWIGRUBY
%typemap(argout) apr_array_header_t **OUTPUT_OF_PROP {
  %append_output(svn_swig_rb_apr_array_to_array_prop(*$1));
}
%apply apr_array_header_t **OUTPUT_OF_PROP {
  apr_array_header_t **entry_props,
  apr_array_header_t **wc_props,
  apr_array_header_t **regular_props
};
#endif

/* -----------------------------------------------------------------------
   Output of apr_array_header_t * <const char *>
*/

#ifdef SWIGPYTHON
%typemap(argout) apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
  %append_output(svn_swig_py_array_to_list(*$1));
}
#endif
#ifdef SWIGPERL
%typemap(argout) apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
  %append_output(svn_swig_pl_array_to_list(*$1));
}
#endif
#ifdef SWIGRUBY
%typemap(argout) apr_array_header_t **OUTPUT_OF_CONST_CHAR_P {
  %append_output(svn_swig_rb_apr_array_to_array_string(*$1));
}
#endif

/* svn_prop_diffs */
#ifdef SWIGRUBY
%typemap(argout) apr_array_header_t **propdiffs {
  %append_output(svn_swig_rb_apr_array_to_array_prop(*$1));
}
#endif

/* -----------------------------------------------------------------------
   apr_array_header_t **externals_p
   svn_wc_parse_externals_description2()
*/

#ifdef SWIGRUBY
%typemap(argout) apr_array_header_t **externals_p {
  %append_output(svn_swig_rb_apr_array_to_array_external_item(*$1));
}
#endif

/* -----------------------------------------------------------------------
   apr_array_header_t **patterns
   svn_wc_get_default_ignores()
*/

#ifdef SWIGRUBY
%typemap(argout) apr_array_header_t **patterns {
  %append_output(svn_swig_rb_apr_array_to_array_string(*$1));
}
#endif

#ifdef SWIGPYTHON
%typemap(argout) apr_array_header_t **patterns {
  %append_output(svn_swig_py_array_to_list(*$1));
}
#endif

/* svn_client_update2 */
#ifdef SWIGRUBY
%typemap(argout) apr_array_header_t **result_revs {
  %append_output(svn_swig_rb_apr_array_to_array_svn_rev(*$1));
}
#endif

/* =======================================================================
   %typemap(in) apr_array_header_t *
*/

/* -----------------------------------------------------------------------
   apr_array_header_t *location_revisions
   svn_repos_trace_node_locations()
   svn_ra_get_locations()
*/

#ifdef SWIGRUBY
%typemap(in) apr_array_header_t *location_revisions
{
  $1 = svn_swig_rb_array_to_apr_array_revnum($input, _global_pool);
}
#endif

/* -----------------------------------------------------------------------
   define a general INPUT param of an array of const char * items.
 */

#ifdef SWIGPYTHON
%typemap(in) const apr_array_header_t *STRINGLIST {
    $1 = (apr_array_header_t *) svn_swig_py_strings_to_array($input,
                                                             _global_pool);
    if ($1 == NULL)
        SWIG_fail;
}
#endif
#ifdef SWIGPERL
%typemap(in) const apr_array_header_t *STRINGLIST {
    $1 = (apr_array_header_t *) svn_swig_pl_strings_to_array($input,
                                                             _global_pool);
}
#endif

#ifdef SWIGRUBY
%typemap(in) const apr_array_header_t *STRINGLIST {
  $1 = svn_swig_rb_strings_to_apr_array($input, _global_pool);
}
#endif

/* path lists */
%apply const apr_array_header_t *STRINGLIST {
    const apr_array_header_t *paths
};

/* -----------------------------------------------------------------------
   auth provider convertors
*/
#ifdef SWIGPERL
%typemap(in) apr_array_header_t *providers {
    $1 = (apr_array_header_t *) svn_swig_pl_objs_to_array($input,
      $descriptor(svn_auth_provider_object_t *), _global_pool);
}
#endif

#ifdef SWIGPYTHON
%typemap(in) apr_array_header_t *providers {
    svn_auth_provider_object_t *provider;
    int targlen;
    if (!PySequence_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "not a sequence");
        SWIG_fail;
    }
    targlen = PySequence_Length($input);
    $1 = apr_array_make(_global_pool, targlen, sizeof(provider));
    ($1)->nelts = targlen;
    while (targlen--) {
        provider = svn_swig_MustGetPtr(PySequence_GetItem($input, targlen),
          $descriptor(svn_auth_provider_object_t *), $svn_argnum, NULL);
        if (PyErr_Occurred()) {
          SWIG_fail;
        }
        APR_ARRAY_IDX($1, targlen, svn_auth_provider_object_t *) = provider;
    }
}
#endif

#ifdef SWIGRUBY
%typemap(in) apr_array_header_t *providers
{
  $1 = svn_swig_rb_array_to_auth_provider_object_apr_array($input, _global_pool);
}
#endif

#ifdef SWIGRUBY
%typemap(in) apr_array_header_t *proplist
{
  $1 = svn_swig_rb_array_to_apr_array_prop($input, _global_pool);
}
#endif

/* svn_delta_path_driver() */
#ifdef SWIGRUBY
%typemap(in) apr_array_header_t *paths
{
  $1 = svn_swig_rb_strings_to_apr_array($input, _global_pool);
}
#endif

/* -----------------------------------------------------------------------
   handle svn_ra_get_locations()
*/
#ifdef SWIGPYTHON
%typemap(in) apr_array_header_t *location_revisions {
    $1 = (apr_array_header_t *) svn_swig_py_revnums_to_array($input,
                                                             _global_pool);
    if ($1 == NULL)
        SWIG_fail;
}
#endif

/* -----------------------------------------------------------------------
   handle the 'location_revisions' parameter appropriately
*/
#ifdef SWIGPYTHON
%typemap(in) apr_array_header_t *location_revisions {
    $1 = (apr_array_header_t *) svn_swig_py_revnums_to_array($input,
                                                             _global_pool);
    if ($1 == NULL)
        SWIG_fail;
}
#endif

/* -----------------------------------------------------------------------
   apr_array_header_t *wcprop_changes
   svn_wc_process_committed2()
*/

#ifdef SWIGRUBY
%typemap(in) apr_array_header_t *wcprop_changes
{
  VALUE rb_pool;
  apr_pool_t *pool;

  svn_swig_rb_get_pool(argc, argv, self, &rb_pool, &pool);

  $1 = svn_swig_rb_array_to_apr_array_prop($input, pool);
}
#endif

/* -----------------------------------------------------------------------
   apr_array_header_t *propchanges
   svn_wc_merge_props()
*/
#ifdef SWIGRUBY
%typemap(in) apr_array_header_t *propchanges
{
  VALUE rb_pool;
  apr_pool_t *pool;

  svn_swig_rb_get_pool(argc, argv, self, &rb_pool, &pool);

  $1 = svn_swig_rb_array_to_apr_array_prop($input, pool);
}
#endif

/* -----------------------------------------------------------------------
   apr_array_header_t *merge_options
   svn_wc_merge2()
*/
#ifdef SWIGRUBY
%typemap(in) apr_array_header_t *merge_options
{
  if (NIL_P($input)) {
    $1 = NULL;
  } else {
    VALUE rb_pool;
    apr_pool_t *pool;

    svn_swig_rb_get_pool(argc, argv, self, &rb_pool, &pool);
    $1 = svn_swig_rb_array_to_apr_array_prop($input, pool);
  }
}
#endif

