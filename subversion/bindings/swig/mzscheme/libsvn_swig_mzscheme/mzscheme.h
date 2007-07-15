#ifndef SVN_SWIG_SWIGUTIL_MZSCM_H
#define SVN_SWIG_SWIGUTIL_MZSCM_H

#include <regex.h>

#include <apr.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_portable.h>
#include <apr_file_io.h>
#include <escheme.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_repos.h"


/* Define DLL export magic on Windows for scheme. */
#ifdef WIN32
#  ifdef SVN_SWIG_SWIGUTIL_MZSCM_C
#    define SVN_MZSCM_SWIG_SWIGUTIL_EXPORT __declspec(dllexport)
#  else
#    define SVN_MZSCM_SWIG_SWIGUTIL_EXPORT __declspec(dllimport)
#  endif
#else /* WIN32 */
#  define SVN_MZSCM_SWIG_SWIGUTIL_EXPORT
#endif /* WIN32 */


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct apr_pool_wrapper_t
{
  apr_pool_t *pool;
  svn_boolean_t destroyed;
  struct apr_pool_wrapper_t *parent;
  apr_array_header_t *children;
} apr_pool_wrapper_t;

/* Initialize the libsvn_swig_py library. */
SVN_MZSCM_SWIG_SWIGUTIL_EXPORT
apr_status_t svn_swig_mzscm_initialize(void);

SVN_MZSCM_SWIG_SWIGUTIL_EXPORT
svn_error_t * svn_swig_mzscm_repos_history_func (void *baton,
                               const char *path,
                               svn_revnum_t revision,
				   apr_pool_t *pool);
SVN_MZSCM_SWIG_SWIGUTIL_EXPORT
svn_error_t * svn_swig_mzscm_repos_file_rev_handler(void *baton,
                                   const char *path,
                                   svn_revnum_t rev,
                                   apr_hash_t *rev_props,
                                   svn_txdelta_window_handler_t *delta_handler,
                                   void **delta_baton,
                                   apr_array_header_t *prop_diffs,
				      apr_pool_t *pool);
SVN_MZSCM_SWIG_SWIGUTIL_EXPORT
svn_error_t * svn_swig_mzscm_wc_relocation_validator3(void *baton,
                                     const char *uuid,
                                     const char *url,
                                     const char *root_url,
					apr_pool_t *pool);
SVN_MZSCM_SWIG_SWIGUTIL_EXPORT
svn_error_t * svn_swig_mzscm_repos_authz_func(svn_boolean_t *allowed,
                             svn_fs_root_t *root,
                             const char *path,
                             void *baton,
				apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* SVN_SWIG_SWIGUTIL_MZSCM_H */


