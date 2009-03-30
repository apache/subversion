/*
 * db-test.c :  test the wc_db subsystem
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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

#include <stdlib.h>
#include <stdio.h>

#include <apr_pools.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_dirent_uri.h"


static void
str_value(const char *name, const char *value)
{
  if (value == NULL)
    printf("e.%s = None\n", name);
  else
    printf("e.%s = '%s'\n", name, value);
}
  

static void
int_value(const char *name, long int value)
{
  printf("e.%s = %ld\n", name, value);
}


static void
bool_value(const char *name, svn_boolean_t value)
{
  if (value)
    printf("e.%s = True\n", name);
  else
    printf("e.%s = False\n", name);
}

static svn_error_t *
entries_dump(const char *dir_path, apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_boolean_t locked;

  SVN_ERR(svn_wc_locked(&locked, dir_path, pool));
  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, dir_path, FALSE, 0,
                           NULL, NULL, pool));
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, pool));

  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *value;
      const svn_wc_entry_t *entry;

      apr_hash_this(hi, &key, NULL, &value);
      entry = value;

      SVN_ERR_ASSERT(strcmp(key, entry->name) == 0);

      printf("e = Entry()\n");
      int_value("revision", entry->revision);
      str_value("url", entry->url);
      str_value("repos", entry->repos);
      str_value("uuid", entry->uuid);
      int_value("kind", entry->kind);
      int_value("schedule", entry->schedule);
      bool_value("copied", entry->copied);
      bool_value("deleted", entry->deleted);
      bool_value("absent", entry->absent);
      bool_value("incomplete", entry->incomplete);
      str_value("copyfrom_url", entry->copyfrom_url);
      int_value("copyfrom_rev", entry->copyfrom_rev);
      str_value("conflict_old", entry->conflict_old);
      str_value("conflict_new", entry->conflict_new);
      str_value("conflict_wrk", entry->conflict_wrk);
      str_value("prejfile", entry->prejfile);
      /* skip: text_time */
      /* skip: prop_time */
      /* skip: checksum */
      int_value("cmt_rev", entry->cmt_rev);
      /* skip: cmt_date */
      str_value("cmt_author", entry->cmt_author);
      str_value("lock_token", entry->lock_token);
      str_value("lock_owner", entry->lock_owner);
      str_value("lock_comment", entry->lock_comment);
      /* skip: lock_creation_date */
      /* skip: has_props */
      /* skip: has_prop_mods */
      /* skip: cachable_props */
      /* skip: present_props */
      str_value("changelist", entry->changelist);
      /* skip: working_size */
      /* skip: keep_local */
      int_value("depth", entry->depth);
      /* skip: tree_conflict_data */
      /* skip: file_external_path */
      /* skip: file_external_peg_rev */
      /* skip: file_external_rev */
      bool_value("locked", locked && *entry->name == '\0');
      printf("entries['%s'] = e\n", (const char *)key);
    }

  return svn_wc_adm_close2(adm_access, pool);
}


int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;
  const char *path;

  if (argc != 2)
    {
      printf("USAGE: entries-dump DIR_PATH\n");
      exit(1);
    }

  if (apr_initialize() != APR_SUCCESS)
    {
      printf("apr_initialize() failed.\n");
      exit(1);
    }

  /* set up the global pool */
  pool = svn_pool_create(NULL);

  path = svn_dirent_internal_style(argv[1], pool);
  err = entries_dump(path, pool);
  if (err)
    {
      svn_handle_error2(err, stderr, FALSE, "entries-dump: ");
      svn_error_clear(err);
      exit_code = EXIT_FAILURE;
    }

  /* Clean up, and get outta here */
  svn_pool_destroy(pool);
  apr_terminate();

  exit(exit_code);
  return exit_code;
}
