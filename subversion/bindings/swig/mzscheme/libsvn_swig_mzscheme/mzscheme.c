/*
 * mzscheme.c: utility functions for the SWIG Scheme bindings
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

/* Tell mzscheme.h that we're inside the implementation */
#define SVN_SWIG_SWIGUTIL_MZSCM_C

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef _

#include <locale.h>

#include "svn_nls.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "svn_utf.h"



#include <string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <escheme.h>
#include <apr.h>
#include <apr_general.h>
#include <apr_portable.h>

#include "svn_pools.h"
#include "svn_opt.h"


#include "mzscheme.h"


#if APR_HAS_LARGE_FILES
#  define AOFF2NUM(num) LL2NUM(num)
#else
#  define AOFF2NUM(num) LONG2NUM(num)
#endif

#if SIZEOF_LONG_LONG == 8
#  define AI642NUM(num) LL2NUM(num)
#else
#  define AI642NUM(num) LONG2NUM(num)
#endif

apr_status_t svn_swig_mzscheme_initialize(void)
{
  apr_status_t status;

  if ((status = apr_initialize()) != APR_SUCCESS)
    return status;
  if (atexit(apr_terminate) != 0)
    return APR_EGENERAL;
  return APR_SUCCESS;
}

svn_error_t *
svn_swig_mzscm_repos_history_func(void *baton,
                               const char *path,
                               svn_revnum_t revision,
                               apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  /*FIXME: Implement me*/
  return err;
}

svn_error_t *
svn_swig_mzscm_repos_file_rev_handler(void *baton,
                                   const char *path,
                                   svn_revnum_t rev,
                                   apr_hash_t *rev_props,
                                   svn_txdelta_window_handler_t *delta_handler,
                                   void **delta_baton,
                                   apr_array_header_t *prop_diffs,
                                   apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  /*FIXME: Implement me*/
  return err;
}


svn_error_t *
svn_swig_mzscm_wc_relocation_validator3(void *baton,
                                     const char *uuid,
                                     const char *url,
                                     const char *root_url,
                                     apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  /*FIXME: Implement me*/
  return err;
}

svn_error_t *
svn_swig_mzscm_repos_authz_func(svn_boolean_t *allowed,
                             svn_fs_root_t *root,
                             const char *path,
                             void *baton,
                             apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  /*FIXME: Implement me*/
  return err;
}
