/*
 * serve.c :  Functions for serving the Subversion protocol
 *
 * ====================================================================
 * Copyright (c) 2002 CollabNet.  All rights reserved.
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



#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_network_io.h>
#include <apr_user.h>

#include <svn_types.h>
#include <svn_string.h>
#include <svn_pools.h>
#include <svn_error.h>
#include <svn_ra.h>
#include <svn_ra_svn.h>
#include <svn_repos.h>
#include <svn_path.h>
#include <svn_time.h>

#include "server.h"

typedef struct {
  svn_repos_t *repos;
  const char *url;         /* Original URL passed from client */
  const char *repos_url;   /* Decoded URL to base of repository */
  const char *fs_path;     /* Decoded base path inside repository */
  const char *user;
  svn_fs_t *fs;            /* For convenience; same as svn_repos_fs(repos) */
} server_baton_t;

typedef struct {
  svn_revnum_t *new_rev;
  const char **date;
  const char **author;
} commit_callback_baton_t;

typedef struct {
  const char *repos_url;
  void *report_baton;
} report_driver_baton_t;

typedef struct {
  const char *fs_path;
  svn_ra_svn_conn_t *conn;
} log_baton_t;

/* --- REPORTER COMMAND SET --- */

static svn_error_t *set_path(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                             apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;
  const char *path;
  svn_revnum_t rev;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "cr", &path, &rev));
  CMD_ERR(svn_repos_set_path(b->report_baton, path, rev));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *delete_path(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;
  const char *path;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c", &path));
  CMD_ERR(svn_repos_delete_path(b->report_baton, path));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}
    
static svn_error_t *link_path(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                              apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;
  const char *path, *url;
  svn_revnum_t rev;
  int len;
  svn_error_t *err;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "ccr", &path, &url, &rev));
  url = svn_path_uri_decode(url, pool);
  len = strlen(b->repos_url);
  if (strncmp(url, b->repos_url, len) != 0)
    {
      err = svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, 0, NULL,
                               "'%s'\n"
                               "is not the same repository as\n"
                               "'%s'", url, b->repos_url);
      /* Wrap error so that it gets reported back to the client. */
      return svn_error_create(SVN_ERR_RA_SVN_CMD_ERR, 0, err, NULL);
    }
  CMD_ERR(svn_repos_link_path(b->report_baton, path, url + len, rev));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *finish_report(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;

  /* No arguments to parse. */
  /* Finishing a report generally means driving an editor, so we can't
   * do it before sending the command response, and we can't report
   * errors to the client. */
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  SVN_ERR(svn_repos_finish_report(b->report_baton));
  return SVN_NO_ERROR;
}

static svn_error_t *abort_report(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;

  /* No arguments to parse. */
  CMD_ERR(svn_repos_abort_report(b->report_baton));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

svn_ra_svn_cmd_entry_t report_commands[] = {
  { "set-path",      set_path },
  { "delete-path",   delete_path },
  { "link-path",     link_path },
  { "finish-report", finish_report, TRUE },
  { "abort-report",  abort_report,  TRUE },
  { NULL }
};

static svn_error_t *handle_report(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  const char *repos_url, void *baton)
{
  report_driver_baton_t b;

  b.repos_url = repos_url;
  b.report_baton = baton;
  return svn_ra_svn_handle_commands(conn, pool, report_commands, &b, FALSE);
}

/* --- MAIN COMMAND SET --- */

/* Write out a property list.  PROPS is allowed to be NULL, in which case
 * an empty list will be written out; this happens if the client could
 * have asked for props but didn't. */
static svn_error_t *write_proplist(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   apr_hash_t *props)
{
  apr_hash_index_t *hi;
  const void *namevar;
  void *valuevar;
  const char *name;
  svn_string_t *value;

  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  if (props)
    {
      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &namevar, NULL, &valuevar);
          name = namevar;
          value = valuevar;
          SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "cs", name, value));
        }
    }
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  return SVN_NO_ERROR;
}

static const char *kind_word(svn_node_kind_t kind)
{
  switch (kind)
    {
    case svn_node_none:
      return "none";
    case svn_node_file:
      return "file";
    case svn_node_dir:
      return "dir";
    case svn_node_unknown:
      return "unknown";
    default:
      abort();
    }
}

/* ### This really belongs in libsvn_repos. */
/* Get the properties for a path, with hardcoded committed-info values. */
static svn_error_t *get_props(apr_hash_t **props, svn_fs_root_t *root,
                              const char *path, apr_pool_t *pool)
{
  svn_string_t *str;
  svn_revnum_t crev;
  const char *cdate, *cauthor;

  /* Get the properties. */
  SVN_ERR(svn_fs_node_proplist(props, root, path, pool));

  /* Hardcode the values for the committed revision, date, and author. */
  SVN_ERR(svn_repos_get_committed_info(&crev, &cdate, &cauthor, root,
                                       path, pool));
  str = svn_string_create(apr_psprintf(pool, "%" SVN_REVNUM_T_FMT, crev),
                          pool);
  apr_hash_set(*props, SVN_PROP_ENTRY_COMMITTED_REV, APR_HASH_KEY_STRING, str);
  str = (cdate) ? svn_string_create(cdate, pool) : NULL;
  apr_hash_set(*props, SVN_PROP_ENTRY_COMMITTED_DATE, APR_HASH_KEY_STRING,
               str);
  str = (cauthor) ? svn_string_create(cauthor, pool) : NULL;
  apr_hash_set(*props, SVN_PROP_ENTRY_LAST_AUTHOR, APR_HASH_KEY_STRING, str);
  return SVN_NO_ERROR;
}

static svn_error_t *get_latest_rev(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;

  CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *get_dated_rev(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  apr_time_t tm;
  const char *timestr;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c", &timestr));
  CMD_ERR(svn_time_from_cstring(&tm, timestr, pool));
  CMD_ERR(svn_repos_dated_revision(&rev, b->repos, tm, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *change_rev_prop(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                    apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *name;
  svn_string_t *value;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "rcs", &rev, &name, &value));
  CMD_ERR(svn_repos_fs_change_rev_prop(b->repos, rev, b->user, name, value,
                                       pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *rev_proplist(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                 apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  apr_hash_t *props;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "r", &rev));
  CMD_ERR(svn_fs_revision_proplist(&props, b->fs, rev, pool));
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_write_word(conn, pool, "success"));
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(write_proplist(conn, pool, props));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *rev_prop(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                             apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *name;
  svn_string_t *value;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "rc", &rev, &name));
  CMD_ERR(svn_fs_revision_prop(&value, b->fs, rev, name, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, "[s]", value));
  return SVN_NO_ERROR;
}

static svn_error_t *commit_done(svn_revnum_t new_rev, const char *date,
                                const char *author, void *baton)
{
  commit_callback_baton_t *ccb = baton;

  *ccb->new_rev = new_rev;
  *ccb->date = date;
  *ccb->author = author;
  return SVN_NO_ERROR;
}

static svn_error_t *commit(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                           apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  const char *log_msg, *date, *author;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_boolean_t aborted;
  commit_callback_baton_t ccb;
  svn_revnum_t new_rev;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c", &log_msg));
  ccb.new_rev = &new_rev;
  ccb.date = &date;
  ccb.author = &author;
  CMD_ERR(svn_repos_get_commit_editor(&editor, &edit_baton, b->repos,
                                      b->repos_url, b->fs_path, b->user,
                                      log_msg, commit_done, &ccb, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  SVN_ERR(svn_ra_svn_drive_editor(conn, pool, editor, edit_baton, FALSE,
                                  &aborted));
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "rcc", new_rev, date, author));
  return SVN_NO_ERROR;
}

static svn_error_t *get_file(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                             apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  const char *path, *full_path;
  svn_revnum_t rev;
  svn_fs_root_t *root;
  svn_stream_t *contents;
  apr_hash_t *props = NULL;
  svn_string_t write_str;
  char buf[4096];
  apr_size_t len;
  svn_boolean_t want_props, want_contents;

  /* Parse arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c[r]bb", &path, &rev,
                                 &want_props, &want_contents));
  if (!SVN_IS_VALID_REVNUM(rev))
    CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  full_path = svn_path_join(b->fs_path, path, pool);

  /* Fetch the properties and a stream for the contents. */
  CMD_ERR(svn_fs_revision_root(&root, b->fs, rev, pool));
  if (want_props)
    CMD_ERR(get_props(&props, root, full_path, pool));
  if (want_contents)
    CMD_ERR(svn_fs_file_contents(&contents, root, full_path, pool));

  /* Send successful command response with revision and props. */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_write_word(conn, pool, "success"));
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_write_number(conn, pool, rev));
  SVN_ERR(write_proplist(conn, pool, props));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));

  /* Now send the file's contents. */
  if (want_contents)
    {
      while (1)
        {
          len = sizeof(buf);
          SVN_ERR(svn_stream_read(contents, buf, &len));
          write_str.data = buf;
          write_str.len = len;
          if (len > 0)
            SVN_ERR(svn_ra_svn_write_string(conn, pool, &write_str));
          if (len < sizeof(buf))
            {
              SVN_ERR(svn_ra_svn_write_cstring(conn, pool, ""));
              break;
            }
        }
      SVN_ERR(svn_stream_close(contents));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *get_dir(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                            apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  const char *path, *full_path, *file_path, *name, *cauthor, *cdate;
  svn_revnum_t rev;
  apr_hash_t *entries, *props = NULL, *file_props;
  apr_hash_index_t *hi;
  svn_fs_dirent_t *fsent;
  svn_dirent_t *entry;
  const void *key;
  void *val;
  svn_boolean_t is_dir;
  svn_fs_root_t *root;
  apr_pool_t *subpool;
  svn_boolean_t want_props, want_contents;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c[r]bb", &path, &rev,
                                 &want_props, &want_contents));
  if (!SVN_IS_VALID_REVNUM(rev))
    CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  full_path = svn_path_join(b->fs_path, path, pool);

  /* Fetch the root of the appropriate revision. */
  CMD_ERR(svn_fs_revision_root(&root, b->fs, rev, pool));

  /* Fetch the directory properties if requested. */
  if (want_props)
    CMD_ERR(get_props(&props, root, full_path, pool));

  /* Fetch the directory entries if requested. */
  if (want_contents)
    {
      CMD_ERR(svn_fs_dir_entries(&entries, root, full_path, pool));

      /* Transform the hash table's FS entries into dirents.  This probably
       * belongs in libsvn_repos. */
      subpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &key, NULL, &val);
          name = key;
          fsent = val;

          file_path = svn_path_join(full_path, name, subpool);
          entry = apr_pcalloc(pool, sizeof(*entry));

          /* kind */
          CMD_ERR(svn_fs_is_dir(&is_dir, root, file_path, subpool));
          entry->kind = is_dir ? svn_node_dir : svn_node_file;

          /* size */
          if (is_dir)
            entry->size = 0;
          else
            CMD_ERR(svn_fs_file_length(&entry->size, root, file_path,
                                       subpool));

          /* has_props */
          CMD_ERR(svn_fs_node_proplist(&file_props, root, file_path, subpool));
          entry->has_props = (apr_hash_count(file_props) > 0) ? TRUE : FALSE;

          /* created_rev, last_author, time */
          CMD_ERR(svn_repos_get_committed_info(&entry->created_rev, &cdate,
                                               &cauthor, root, file_path,
                                               subpool));
          entry->last_author = apr_pstrdup (pool, cauthor);
          if (cdate)
            CMD_ERR(svn_time_from_cstring(&entry->time, cdate, subpool));
          else
            entry->time = (time_t) -1;

          /* Store the entry. */
          apr_hash_set(entries, name, APR_HASH_KEY_STRING, entry);
          svn_pool_clear(subpool);
        }
      svn_pool_destroy(subpool);
    }

  /* Write out response. */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_write_word(conn, pool, "success"));
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_write_number(conn, pool, rev));
  SVN_ERR(write_proplist(conn, pool, props));
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  if (want_contents)
    {
      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next (hi))
        {
          apr_hash_this(hi, &key, NULL, &val);
          name = key;
          entry = val;
          cdate = (entry->time == (time_t) -1) ? NULL
            : svn_time_to_cstring(entry->time, pool);
          SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "cwnbr[c][c]", name,
                                         kind_word(entry->kind),
                                         (apr_uint64_t) entry->size,
                                         entry->has_props, entry->created_rev,
                                         cdate, entry->last_author));
        }
    }
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *checkout(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                             apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  svn_boolean_t recurse;
  const svn_delta_editor_t *editor;
  void *edit_baton;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "[r]b", &rev, &recurse));
  if (!SVN_IS_VALID_REVNUM(rev))
    CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));

  /* Write an empty command-reponse, signalling that we will start editing. */
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));

  /* Drive the network editor with a checkout. */
  svn_ra_svn_get_editor(&editor, &edit_baton, conn, pool, NULL, NULL);
  SVN_ERR(svn_repos_checkout(b->fs, rev, recurse, b->fs_path,
                             editor, edit_baton, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *update(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                           apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *target;
  svn_boolean_t recurse;
  const svn_delta_editor_t *editor;
  void *edit_baton, *report_baton;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "[r]cb", &rev, &target,
                                 &recurse));
  if (svn_path_is_empty(target))
    target = NULL;  /* ### Compatibility hack, shouldn't be needed */
  if (!SVN_IS_VALID_REVNUM(rev))
    CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));

  /* Make an svn_repos report baton.  Tell it to drive the network editor
   * when the report is complete. */
  svn_ra_svn_get_editor(&editor, &edit_baton, conn, pool, NULL, NULL);
  CMD_ERR(svn_repos_begin_report(&report_baton, rev, b->user, b->repos,
                                 b->fs_path, target, NULL, TRUE, recurse,
                                 editor, edit_baton, pool));

  /* Write an empty command-reponse, telling the client to start reporting. */
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));

  /* Handle the client's report; when it's done, svn_repos will drive the
   * network editor with the update. */
  return handle_report(conn, pool, b->repos_url, report_baton);
}

static svn_error_t *switch_cmd(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                               apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *target;
  const char *switch_url, *switch_path;
  svn_boolean_t recurse;
  const svn_delta_editor_t *editor;
  void *edit_baton, *report_baton;
  int len;
  svn_error_t *err;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "[r]cbc", &rev, &target,
                                 &recurse, &switch_url));
  if (svn_path_is_empty(target))
    target = NULL;  /* ### Compatibility hack, shouldn't be needed */
  if (!SVN_IS_VALID_REVNUM(rev))
    CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));

  /* Verify that switch_url is in the same repository and get its fs path. */
  switch_url = svn_path_uri_decode(switch_url, pool);
  len = strlen(b->repos_url);
  if (strncmp(switch_url, b->repos_url, len) != 0)
    {
      err = svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, 0, NULL,
                               "'%s'\n"
                               "is not the same repository as\n"
                               "'%s'", switch_url, b->repos_url);
      /* Wrap error so that it gets reported back to the client. */
      return svn_error_create(SVN_ERR_RA_SVN_CMD_ERR, 0, err, NULL);
    }
  switch_path = switch_url + len;

  /* Make an svn_repos report baton.  Tell it to drive the network editor
   * when the report is complete. */
  svn_ra_svn_get_editor(&editor, &edit_baton, conn, pool, NULL, NULL);
  CMD_ERR(svn_repos_begin_report(&report_baton, rev, b->user, b->repos,
                                 b->fs_path, target, switch_path, TRUE,
                                 recurse, editor, edit_baton, pool));

  /* Write an empty command-reponse, telling the client to start reporting. */
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));

  /* Handle the client's report; when it's done, svn_repos will drive the
   * network editor with the switch update. */
  return handle_report(conn, pool, b->repos_url, report_baton);
}

static svn_error_t *status(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                           apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *target;
  svn_boolean_t recurse;
  const svn_delta_editor_t *editor;
  void *edit_baton, *report_baton;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "cb", &target, &recurse));
  if (svn_path_is_empty(target))
    target = NULL;  /* ### Compatibility hack, shouldn't be needed */

  /* Make an svn_repos report baton.  Tell it to drive the network editor
   * when the report is complete. */
  CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  svn_ra_svn_get_editor(&editor, &edit_baton, conn, pool, NULL, NULL);
  CMD_ERR(svn_repos_begin_report(&report_baton, rev, b->user, b->repos,
                                 b->fs_path, target, NULL, FALSE, recurse,
                                 editor, edit_baton, pool));

  /* Write an empty command-reponse, telling the client to start reporting. */
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));

  /* Handle the client's report; when it's done, svn_repos will drive the
   * network editor with the status report. */
  return handle_report(conn, pool, b->repos_url, report_baton);
}

static svn_error_t *diff(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                         apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *target, *versus_url, *versus_path;
  svn_boolean_t recurse;
  const svn_delta_editor_t *editor;
  void *edit_baton, *report_baton;

  int len;
  svn_error_t *err;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "[r]cbc", &rev, &target,
                                 &recurse, &versus_url));
  if (svn_path_is_empty(target))
    target = NULL;  /* ### Compatibility hack, shouldn't be needed */
  if (!SVN_IS_VALID_REVNUM(rev))
    CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));

  /* Verify that versus_url is in the same repository and get its fs path. */
  versus_url = svn_path_uri_decode(versus_url, pool);
  len = strlen(b->repos_url);
  if (strncmp(versus_url, b->repos_url, len) != 0)
    {
      err = svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, 0, NULL,
                               "'%s'\n"
                               "is not the same repository as\n"
                               "'%s'", versus_url, b->repos_url);
      /* Wrap error so that it gets reported back to the client. */
      return svn_error_create(SVN_ERR_RA_SVN_CMD_ERR, 0, err, NULL);
    }
  versus_path = versus_url + len;

  /* Make an svn_repos report baton.  Tell it to drive the network editor
   * when the report is complete. */
  svn_ra_svn_get_editor(&editor, &edit_baton, conn, pool, NULL, NULL);
  CMD_ERR(svn_repos_begin_report(&report_baton, rev, b->user, b->repos,
                                 b->fs_path, target, versus_path, FALSE,
                                 recurse, editor, edit_baton, pool));

  /* Write an empty command-reponse, telling the client to start reporting. */
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));

  /* Handle the client's report; when it's done, svn_repos will drive the
   * network editor with the diff. */
  return handle_report(conn, pool, b->repos_url, report_baton);
}

/* Send a log entry to the client. */
static svn_error_t *log_receiver(void *baton, apr_hash_t *changed_paths,
                                 svn_revnum_t rev, const char *author,
                                 const char *date, const char *message,
                                 apr_pool_t *pool)
{
  log_baton_t *b = baton;
  svn_ra_svn_conn_t *conn = b->conn;
  apr_hash_index_t *h;
  const void *key;
  void *val;
  const char *path;
  svn_log_changed_path_t *change;
  char action[2];

  SVN_ERR(svn_ra_svn_start_list(conn, pool));

  /* Element 1: changed paths */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  if (changed_paths)
    {
      for (h = apr_hash_first(pool, changed_paths); h; h = apr_hash_next(h))
        {
          apr_hash_this(h, &key, NULL, &val);
          path = key;
          change = val;
          action[0] = change->action;
          action[1] = '\0';
          SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "cw[cr]", path, action,
                                         change->copyfrom_path,
                                         change->copyfrom_rev));
        }
    }
  SVN_ERR(svn_ra_svn_end_list(conn, pool));

  /* Element 2: revision number */
  SVN_ERR(svn_ra_svn_write_number(conn, pool, rev));

  /* Element 3: author */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  if (author)
    SVN_ERR(svn_ra_svn_write_cstring(conn, pool, author));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));

  /* Element 4: date */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  if (date)
    SVN_ERR(svn_ra_svn_write_cstring(conn, pool, date));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));

  /* Element 5: message */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  if (message)
    SVN_ERR(svn_ra_svn_write_cstring(conn, pool, message));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));

  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *log_cmd(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                            apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t start_rev, end_rev;
  const char *full_path;
  svn_boolean_t changed_paths, strict_node;
  apr_array_header_t *paths, *full_paths;
  svn_ra_svn_item_t *elt;
  int i;
  log_baton_t lb;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "l[r][r]bb", &paths, &start_rev,
                                 &end_rev, &changed_paths, &strict_node));
  full_paths = apr_array_make(pool, paths->nelts, sizeof(const char *));
  for (i = 0; i < paths->nelts; i++)
    {
      elt = &((svn_ra_svn_item_t *) paths->elts)[i];
      if (elt->kind != SVN_RA_SVN_STRING)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                                "Log path entry not a string");
      full_path = svn_path_join(b->fs_path, elt->u.string->data, pool);
      *((const char **) apr_array_push(full_paths)) = full_path;
    }

  /* Write an empty command-reponse, telling the client logs are coming. */
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));

  /* Get logs.  (Can't report errors back to the client at this point.) */
  lb.fs_path = b->fs_path;
  lb.conn = conn;
  SVN_ERR(svn_repos_get_logs(b->repos, full_paths, start_rev, end_rev,
                             changed_paths, strict_node, log_receiver, &lb,
                             pool));

  return svn_ra_svn_write_word(conn, pool, "done");
}

static svn_error_t *check_path(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                               apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *path, *full_path;
  svn_fs_root_t *root;
  svn_node_kind_t kind;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c[r]", &path, &rev));
  if (!SVN_IS_VALID_REVNUM(rev))
    SVN_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  full_path = svn_path_join(b->fs_path, path, pool);
  CMD_ERR(svn_fs_revision_root(&root, b->fs, rev, pool));
  kind = svn_fs_check_path(root, full_path, pool);
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, "w", kind_word(kind)));
  return SVN_NO_ERROR;
}

svn_ra_svn_cmd_entry_t main_commands[] = {
  { "get-latest-rev",  get_latest_rev },
  { "get-dated-rev",   get_dated_rev },
  { "change-rev-prop", change_rev_prop },
  { "rev-proplist",    rev_proplist },
  { "rev-prop",        rev_prop },
  { "commit",          commit },
  { "get-file",        get_file },
  { "get-dir",         get_dir },
  { "checkout",        checkout },
  { "update",          update },
  { "switch",          switch_cmd },
  { "status",          status },
  { "diff",            diff },
  { "log",             log_cmd },
  { "check-path",      check_path },
  { NULL }
};

static svn_error_t *find_repos(const char *url, const char *root,
                               svn_repos_t **repos, const char **repos_url,
                               const char **fs_path, apr_pool_t *pool)
{
  svn_error_t *err;
  const char *client_path, *full_path, *candidate;

  /* Decode any escaped characters in the URL. */
  url = svn_path_uri_decode(url, pool);

  /* Verify the scheme part. */
  if (strncmp(url, "svn://", 6) != 0)
    return svn_error_createf(SVN_ERR_BAD_URL, 0, NULL,
                             "Non-svn URL passed to svn server: %s", url);

  /* Skip past the authority part. */
  client_path = strchr(url + 6, '/');
  client_path = (client_path == NULL) ? "" : client_path + 1;

  /* Join the server-configured root with the client path. */
  full_path = svn_path_join(svn_path_canonicalize(root, pool),
                            svn_path_canonicalize(client_path, pool),
                            pool);

  /* Search for a repository in the full path. */
  candidate = full_path;
  while (1)
    {
      err = svn_repos_open(repos, candidate, pool);
      if (err == SVN_NO_ERROR)
        break;
      if (!*candidate || strcmp(candidate, "/") == 0)
        return svn_error_createf(SVN_ERR_RA_SVN_REPOS_NOT_FOUND, 0, NULL,
                                 "No repository found in %s", url);
      candidate = svn_path_dirname(candidate, pool);
    }
  *fs_path = apr_pstrdup(pool, full_path + strlen(candidate));
  *repos_url = apr_pstrmemdup(pool, url, strlen(url) - strlen(*fs_path));
  return SVN_NO_ERROR;
}

svn_error_t *serve(svn_ra_svn_conn_t *conn, const char *root,
                   svn_boolean_t tunnel, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_uint64_t ver;
  const char *mech, *mecharg, *user = NULL, *client_url, *repos_url, *fs_path;
  apr_array_header_t *caplist;
  svn_repos_t *repos;
  server_baton_t b;

  /* Send greeting, saying we only support protocol version 1, the
   * anonymous authentication mechanism, and no extensions. */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  /* Our minimum and maximum supported protocol version is 1. */
  SVN_ERR(svn_ra_svn_write_number(conn, pool, 1));
  SVN_ERR(svn_ra_svn_write_number(conn, pool, 1));
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  /* We support anonymous and maybe external authentication. */
  SVN_ERR(svn_ra_svn_write_word(conn, pool, "ANONYMOUS"));
#if APR_HAS_USER
  if (tunnel)
    SVN_ERR(svn_ra_svn_write_word(conn, pool, "EXTERNAL"));
#endif
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  /* We have no special capabilities. */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));

  /* Read client response.  This should specify version 1, the
   * mechanism, a mechanism argument, and possibly some
   * capabilities. */
  SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "nw[c]l", &ver, &mech, &mecharg,
                                &caplist));

#if APR_HAS_USER
  if (tunnel && strcmp(mech, "EXTERNAL") == 0)
    {
      apr_uid_t uid;
      apr_gid_t gid;

      if (!mecharg)  /* Must be present */
        return SVN_NO_ERROR;
      if (apr_current_userid(&uid, &gid, pool) != APR_SUCCESS
          || apr_get_username((char **) &user, uid, pool) != APR_SUCCESS)
        {
          SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w(c)", "failure",
                                         "Can't determine username"));
          return SVN_NO_ERROR;
        }
      if (*mecharg && strcmp(mecharg, user) != 0)
        {
          SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w(c)", "failure",
                                         "Requested username does not match"));
          return SVN_NO_ERROR;
        }
    }
#endif

  if (strcmp(mech, "ANONYMOUS") == 0)
    user = "anonymous";

  if (!user)  /* Client gave us an unlisted mech. */
    return SVN_NO_ERROR;

  /* Write back a success notification. */
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w()", "success"));

  /* This is where the security layer would go into effect if we
   * supported security layers, which is a ways off. */

  /* Read client's URL. */
  SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "c", &client_url));

  err = find_repos(client_url, root, &repos, &repos_url, &fs_path, pool);
  if (err)
    svn_ra_svn_write_cmd_failure(conn, pool, err);
  else
    svn_ra_svn_write_cmd_response(conn, pool, "");
  svn_error_clear(err);

  b.repos = repos;
  b.url = client_url;
  b.repos_url = repos_url;
  b.fs_path = fs_path;
  b.user = user;
  b.fs = svn_repos_fs(repos);
  err = svn_ra_svn_handle_commands(conn, pool, main_commands, &b, FALSE);
  svn_error_clear(svn_repos_close(repos));
  return err;
}
