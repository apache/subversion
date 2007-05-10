#ifndef MOD_AUTHZ_SVN_H
#define MOD_AUTHZ_SVN_H

#include <httpd.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * mod_dav_svn to mod_authz_svn bypass mechanism
 */
#define AUTHZ_SVN__SUBREQ_BYPASS_PROV_GRP "dav2authz_subreq_bypass"
#define AUTHZ_SVN__SUBREQ_BYPASS_PROV_NAME "mod_authz_svn_subreq_bypass"
#define AUTHZ_SVN__SUBREQ_BYPASS_PROV_VER "00.00a"
typedef int (*authz_svn__subreq_bypass_func_t)(request_rec *r,
                                              const char *repos_path,
                                              const char *repos_name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
