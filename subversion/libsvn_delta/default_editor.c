/*
 * default_editor.c -- provide a basic svn_delta_editor_t
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_delta.h"


static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}
static svn_error_t *
add_item(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **baton)
{
  *baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
single_baton_func(void *baton,
                  apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
absent_xxx_func(const char *path,
                void *baton,
                apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  *root_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
open_item(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **baton)
{
  *baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
change_prop(void *file_baton,
            const char *name,
            const svn_string_t *value,
            apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

svn_error_t *svn_delta_noop_window_handler(svn_txdelta_window_t *window,
                                           void *baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  *handler = svn_delta_noop_window_handler;
  *handler_baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}



static const svn_delta_editor_t default_editor =
{
  set_target_revision,
  open_root,
  delete_entry,
  add_item,
  open_item,
  change_prop,
  single_baton_func,
  absent_xxx_func,
  add_item,
  open_item,
  apply_textdelta,
  change_prop,
  close_file,
  absent_xxx_func,
  single_baton_func,
  single_baton_func
};

svn_delta_editor_t *
svn_delta_default_editor(apr_pool_t *pool)
{
  return apr_pmemdup(pool, &default_editor, sizeof(default_editor));
}
