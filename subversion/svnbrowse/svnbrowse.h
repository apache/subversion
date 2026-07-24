/*
 * svnbrowse.h: shared stuff for svnbrowse application
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifndef SVN_SVNBROWSE_SVNBROWSE_H
#define SVN_SVNBROWSE_SVNBROWSE_H

#include <apr.h>

#include "svn_client.h"
#include "svn_opt.h"
#include "svn_ra.h"

enum {
  opt_auth_password = SVN_OPT_FIRST_LONGOPT_ID,
  opt_auth_password_from_stdin,
  opt_auth_username,
  opt_config_dir,
  opt_config_option,
  opt_no_auth_cache,
  opt_version,
  opt_trust_server_cert,
  opt_trust_server_cert_failures,
  opt_password_from_stdin,
};

typedef struct svn_browse__opt_state_t {
  svn_boolean_t version;         /* print version information */
  svn_boolean_t verbose;         /* for svnbrowse --version */
  svn_boolean_t quiet;           /* for svnbrowse --version */
  svn_boolean_t help;            /* print usage message */

  const char *auth_username;     /* auth username */
  const char *auth_password;     /* auth password */
  svn_boolean_t no_auth_cache;   /* do not cache authentication information */
  const char *config_dir;        /* over-riding configuration directory */
  apr_array_header_t *config_options; /* over-riding configuration options */
  svn_opt_revision_t revision;   /* --revision */
  const char *path_or_url;
  svn_opt_revision_t peg_revision; /* @PEGREV*/

  /* trust server SSL certs that would otherwise be rejected as "untrusted" */
  svn_boolean_t trust_server_cert_unknown_ca;
  svn_boolean_t trust_server_cert_cn_mismatch;
  svn_boolean_t trust_server_cert_expired;
  svn_boolean_t trust_server_cert_not_yet_valid;
  svn_boolean_t trust_server_cert_other_failure;
} svn_browse__opt_state_t;

typedef struct svn_browse__item_t {
  const char *name;
  const svn_dirent_t *dirent;
} svn_browse__item_t;

typedef enum svn_browse__state_type_e {
  svn_browse__state_dir,
  svn_browse__state_file,
} svn_browse__state_type_e;

/* a state of a single directory */
typedef struct svn_browse__state_t {
  svn_browse__state_type_e type;

  /* information about this node */
  const char *relpath;
  svn_revnum_t revision;

  /* stores the list of nodes in this state; an array of svn_browse__item_t or
   * NULL if 'type' is set to svn_browse__state_file */
  apr_array_header_t *list;

  /* only available for files */
  const svn_dirent_t *this_dirent;

  /* the index of hovered item */
  int selection;
  int scroller_offset;

  /* a pool where the structure is allocated */
  apr_pool_t *pool;
} svn_browse__state_t;

typedef struct svn_browse__model_t {
  const char *root;
  svn_revnum_t revision;

  svn_client_ctx_t *client;
  svn_ra_session_t *session;

  svn_browse__state_t *current;
  apr_pool_t *pool;
} svn_browse__model_t;

svn_error_t *
svn_browse__state_create(svn_browse__state_t **state_p,
                         svn_ra_session_t *session,
                         const char *relpath,
                         svn_revnum_t revision,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

svn_error_t *
svn_browse__model_enter_path(svn_browse__model_t *ctx,
                             const char *relpath,
                             apr_pool_t *scratch_pool);

svn_browse__item_t *
svn_browse__model_get_selected_item(svn_browse__model_t *model);

svn_error_t *
svn_browse__model_go_enter(svn_browse__model_t *model,
                           apr_pool_t *scratch_pool);

svn_error_t *
svn_browse__model_go_up(svn_browse__model_t *model,
                        apr_pool_t *scratch_pool);

svn_error_t *
svn_browse__model_move_selection(svn_browse__model_t *model,
                                 int delta);

svn_error_t *
svn_browse__model_scroll_in_view(svn_browse__model_t *model,
                                 int scroller_height);

svn_error_t *
svn_browse__model_create(svn_browse__model_t **model_p,
                         svn_client_ctx_t *ctx,
                         const char *path_or_url,
                         const svn_opt_revision_t *peg_revision,
                         const svn_opt_revision_t *revision,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

#endif /* SVN_SVNBROWSE_SVNBROWSE_H */
