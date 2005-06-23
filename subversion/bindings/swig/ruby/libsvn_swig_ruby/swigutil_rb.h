#ifndef SVN_SWIG_SWIGUTIL_RB_H
#define SVN_SWIG_SWIGUTIL_RB_H

#include <ruby.h>
#include <regex.h>

#include <apr.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_portable.h>
#include <apr_file_io.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_repos.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <rubyio.h>

VALUE svn_swig_rb_svn_error_new(VALUE code, VALUE message);

VALUE svn_swig_rb_apr_hash_to_hash_string(apr_hash_t *hash);
VALUE svn_swig_rb_apr_hash_to_hash_swig_type(apr_hash_t *hash,
                                             const char *type_name);

VALUE svn_swig_rb_apr_array_to_array_string(const apr_array_header_t *ary);
VALUE svn_swig_rb_apr_array_to_array_svn_string(const apr_array_header_t *ary);
VALUE svn_swig_rb_apr_array_to_array_svn_rev(const apr_array_header_t *ary);
VALUE svn_swig_rb_apr_array_to_array_prop(const apr_array_header_t *ary);

apr_hash_t *svn_swig_rb_hash_to_apr_hash_string(VALUE hash, apr_pool_t *pool);
apr_hash_t *svn_swig_rb_hash_to_apr_hash_svn_string(VALUE hash,
                                                    apr_pool_t *pool);
apr_hash_t *svn_swig_rb_hash_to_apr_hash_swig_type(VALUE hash,
                                                   const char *typename,
                                                   apr_pool_t *pool);

apr_array_header_t *svn_swig_rb_strings_to_apr_array(VALUE strings,
                                                     apr_pool_t *pool);
apr_array_header_t *
svn_swig_rb_array_to_auth_provider_object_apr_array(VALUE array,
                                                    apr_pool_t *pool);
apr_array_header_t * svn_swig_rb_array_to_apr_array_prop(VALUE array,
                                                         apr_pool_t *pool);
  
void svn_swig_rb_get_pool(int argc, VALUE *argv, VALUE self, VALUE *rb_pool, apr_pool_t **pool);
void svn_swig_rb_set_pool(VALUE target, VALUE pool);

void svn_swig_rb_make_editor(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             VALUE rb_editor,
                             apr_pool_t *pool);

svn_error_t *svn_swig_rb_log_receiver(void *baton,
                                      apr_hash_t *changed_paths,
                                      svn_revnum_t revision,
                                      const char *author,
                                      const char *date,
                                      const char *message,
                                      apr_pool_t *pool);
  
svn_error_t *svn_swig_rb_repos_authz_func(svn_boolean_t *allowed,
                                          svn_fs_root_t *root,
                                          const char *path,
                                          void *baton,
                                          apr_pool_t *pool);
  
svn_error_t *svn_swig_rb_get_commit_log_func(const char **log_msg,
                                             const char **tmp_file,
                                             apr_array_header_t *commit_items,
                                             void *baton,
                                             apr_pool_t *pool);
  
/* auth provider callbacks */
svn_error_t *svn_swig_rb_auth_simple_prompt_func(
    svn_auth_cred_simple_t **cred,
    void *baton,
    const char *realm,
    const char *username,
    svn_boolean_t may_save,
    apr_pool_t *pool);

svn_error_t *svn_swig_rb_auth_username_prompt_func(
    svn_auth_cred_username_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool);

svn_error_t *svn_swig_rb_auth_ssl_server_trust_prompt_func(
    svn_auth_cred_ssl_server_trust_t **cred,
    void *baton,
    const char *realm,
    apr_uint32_t failures,
    const svn_auth_ssl_server_cert_info_t *cert_info,
    svn_boolean_t may_save,
    apr_pool_t *pool);

svn_error_t *svn_swig_rb_auth_ssl_client_cert_prompt_func(
    svn_auth_cred_ssl_client_cert_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool);

svn_error_t *svn_swig_rb_auth_ssl_client_cert_pw_prompt_func(
    svn_auth_cred_ssl_client_cert_pw_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool);

apr_file_t *svn_swig_rb_make_file(VALUE file, apr_pool_t *pool);
svn_stream_t *svn_swig_rb_make_stream(VALUE io);

void svn_swig_rb_set_revision(svn_opt_revision_t *rev, VALUE value);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_SWIG_SWIGUTIL_RB_H */
