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

typedef struct apr_pool_wrapper_t
{
  apr_pool_t *pool;
  svn_boolean_t destroyed;
  struct apr_pool_wrapper_t *parent;
  apr_array_header_t *children;
} apr_pool_wrapper_t;

void svn_swig_rb_initialize(void);

VALUE svn_swig_rb_svn_delta_editor(void);
VALUE svn_swig_rb_svn_delta_text_delta_window_handler(void);

VALUE svn_swig_rb_svn_error_new(VALUE code, VALUE message,
                                VALUE file, VALUE line);
void svn_swig_rb_handle_svn_error(svn_error_t *error);

void *svn_swig_rb_to_swig_type(VALUE value, void *ctx, apr_pool_t *pool);
VALUE svn_swig_rb_from_swig_type(void *value, void *ctx);

VALUE svn_swig_rb_apr_hash_to_hash_string(apr_hash_t *hash);
VALUE svn_swig_rb_apr_hash_to_hash_svn_string(apr_hash_t *hash);
VALUE svn_swig_rb_apr_hash_to_hash_swig_type(apr_hash_t *hash,
                                             const char *type_name);

VALUE svn_swig_rb_prop_hash_to_hash(apr_hash_t *prop_hash);
VALUE svn_swig_rb_apr_revnum_key_hash_to_hash_string(apr_hash_t *hash);

VALUE svn_swig_rb_apr_array_to_array_string(const apr_array_header_t *ary);
VALUE svn_swig_rb_apr_array_to_array_svn_string(const apr_array_header_t *ary);
VALUE svn_swig_rb_apr_array_to_array_svn_rev(const apr_array_header_t *ary);
VALUE svn_swig_rb_apr_array_to_array_prop(const apr_array_header_t *ary);
VALUE svn_swig_rb_apr_array_to_array_proplist_item(const apr_array_header_t *ary);
VALUE svn_swig_rb_apr_array_to_array_external_item(const apr_array_header_t *ary);

apr_hash_t *svn_swig_rb_hash_to_apr_hash_string(VALUE hash, apr_pool_t *pool);
apr_hash_t *svn_swig_rb_hash_to_apr_hash_svn_string(VALUE hash,
                                                    apr_pool_t *pool);
apr_hash_t *svn_swig_rb_hash_to_apr_hash_swig_type(VALUE hash,
                                                   const char *typename,
                                                   apr_pool_t *pool);
apr_hash_t *svn_swig_rb_hash_to_apr_hash_revnum(VALUE hash,
                                                apr_pool_t *pool);

apr_array_header_t *svn_swig_rb_strings_to_apr_array(VALUE strings,
                                                     apr_pool_t *pool);
apr_array_header_t *
svn_swig_rb_array_to_auth_provider_object_apr_array(VALUE array,
                                                    apr_pool_t *pool);
apr_array_header_t *svn_swig_rb_array_to_apr_array_prop(VALUE array,
                                                        apr_pool_t *pool);
apr_array_header_t *svn_swig_rb_array_to_apr_array_revnum(VALUE array,
                                                          apr_pool_t *pool);
  
void svn_swig_rb_get_pool(int argc, VALUE *argv, VALUE self, VALUE *rb_pool, apr_pool_t **pool);
void svn_swig_rb_set_pool(VALUE target, VALUE pool);
void svn_swig_rb_set_pool_for_no_swig_type(VALUE target, VALUE pool);
void svn_swig_rb_push_pool(VALUE pool);
void svn_swig_rb_pop_pool(VALUE pool);

void svn_swig_rb_make_delta_editor(svn_delta_editor_t **editor,
                                   void **edit_baton,
                                   VALUE rb_editor,
                                   apr_pool_t *pool);

VALUE svn_swig_rb_make_baton(VALUE proc, VALUE pool);
void svn_swig_rb_set_baton(VALUE target, VALUE baton);

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
  
svn_error_t *svn_swig_rb_repos_authz_callback(svn_repos_authz_access_t required,
                                              svn_boolean_t *allowed,
                                              svn_fs_root_t *root,
                                              const char *path,
                                              void *baton,
                                              apr_pool_t *pool);
  
svn_error_t *svn_swig_rb_get_commit_log_func2(const char **log_msg,
                                              const char **tmp_file,
                                              const apr_array_header_t *commit_items,
                                              void *baton,
                                              apr_pool_t *pool);

void svn_swig_rb_notify_func2(void *baton,
                              const svn_wc_notify_t *notify,
                              apr_pool_t *pool);

svn_error_t *svn_swig_rb_commit_callback(svn_revnum_t new_revision,
                                         const char *date,
                                         const char *author,
                                         void *baton);
svn_error_t *svn_swig_rb_commit_callback2(const svn_commit_info_t *commit_info,
                                          void *baton,
                                          apr_pool_t *pool);
svn_error_t *svn_swig_rb_cancel_func(void *cancel_baton);

svn_error_t *svn_swig_rb_info_receiver(void *baton,
                                       const char *path,
                                       const svn_info_t *info,
                                       apr_pool_t *pool);

svn_boolean_t svn_swig_rb_config_enumerator(const char *name,
                                            const char *value,
                                            void *baton,
                                            apr_pool_t *pool);
svn_boolean_t svn_swig_rb_config_section_enumerator(const char *name,
                                                    void *baton,
                                                    apr_pool_t *pool);

svn_error_t *svn_swig_rb_delta_path_driver_cb_func(void **dir_baton,
                                                   void *parent_baton,
                                                   void *callback_baton,
                                                   const char *path,
                                                   apr_pool_t *pool);

svn_error_t *svn_swig_rb_txdelta_window_handler(svn_txdelta_window_t *window,
                                                void *baton);

void svn_swig_rb_fs_warning_callback(void *baton, svn_error_t *err);
void svn_swig_rb_fs_warning_callback_baton_register(VALUE baton,
                                                    apr_pool_t *pool);

svn_error_t *svn_swig_rb_fs_get_locks_callback(void *baton,
                                               svn_lock_t *lock,
                                               apr_pool_t *pool);

svn_error_t *svn_swig_rb_just_call(void *baton);

void svn_swig_rb_setup_ra_callbacks(svn_ra_callbacks2_t **callbacks,
                                    void **baton,
                                    VALUE rb_callbacks,
                                    apr_pool_t *pool);

svn_error_t *svn_swig_rb_ra_lock_callback(void *baton,
                                          const char *path,
                                          svn_boolean_t do_lock,
                                          const svn_lock_t *lock,
                                          svn_error_t *ra_err,
                                          apr_pool_t *pool);

svn_error_t *svn_swig_rb_ra_file_rev_handler(void *baton,
                                             const char *path,
                                             svn_revnum_t rev,
                                             apr_hash_t *rev_props,
                                             svn_txdelta_window_handler_t *delta_handler,
                                             void **delta_baton,
                                             apr_array_header_t *prop_diffs,
                                             apr_pool_t *pool);

svn_error_t *svn_swig_rb_repos_history_func(void *baton,
                                            const char *path,
                                            svn_revnum_t revision,
                                            apr_pool_t *pool);

svn_error_t *svn_swig_rb_repos_file_rev_handler(void *baton,
                                                const char *path,
                                                svn_revnum_t rev,
                                                apr_hash_t *rev_props,
                                                svn_txdelta_window_handler_t *delta_handler,
                                                void **delta_baton,
                                                apr_array_header_t *prop_diffs,
                                                apr_pool_t *pool);

svn_error_t *svn_swig_rb_wc_relocation_validator2(void *baton,
                                                  const char *uuid,
                                                  const char *url,
                                                  svn_boolean_t root,
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

void svn_swig_rb_adjust_arg_for_client_ctx_and_pool(int *argc, VALUE **argv);


void svn_swig_rb_wc_status_func(void *baton,
                                const char *path,
                                svn_wc_status2_t *status);

svn_error_t *svn_swig_rb_client_blame_receiver_func(void *baton,
                                                    apr_int64_t line_no,
                                                    svn_revnum_t revision,
                                                    const char *author,
                                                    const char *date,
                                                    const char *line,
                                                    apr_pool_t *pool);


svn_wc_entry_callbacks_t *svn_swig_rb_wc_entry_callbacks(void);
svn_wc_diff_callbacks2_t *svn_swig_rb_wc_diff_callbacks2(void);


VALUE svn_swig_rb_make_txdelta_window_handler_wrapper(VALUE *rb_handler_pool,
                                                      apr_pool_t **handler_pool,
                                                      svn_txdelta_window_handler_t **handler,
                                                      void ***handler_baton);
VALUE svn_swig_rb_setup_txdelta_window_handler_wrapper(VALUE obj,
                                                       svn_txdelta_window_handler_t handler,
                                                       void *handler_baton);
svn_error_t *svn_swig_rb_invoke_txdelta_window_handler(VALUE window_handler,
                                                       svn_txdelta_window_t *window,
                                                       apr_pool_t *pool);
svn_error_t *svn_swig_rb_invoke_txdelta_window_handler_wrapper(VALUE obj,
                                                               svn_txdelta_window_t *window,
                                                               apr_pool_t *pool);

VALUE svn_swig_rb_txdelta_window_t_ops_get(svn_txdelta_window_t *window);


svn_error_t *svn_swig_rb_client_diff_summarize_func(const svn_client_diff_summarize_t *diff,
                                                    void *baton,
                                                    apr_pool_t *pool);
svn_error_t *svn_swig_rb_client_list_func(void *baton,
                                          const char *path,
                                          const svn_dirent_t *dirent,
                                          const svn_lock_t *lock,
                                          const char *abs_path,
                                          apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_SWIG_SWIGUTIL_RB_H */
