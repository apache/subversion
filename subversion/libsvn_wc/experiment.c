/*
 * experiment.c :  experiment with the new API described in wc_db.h
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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



#include "svn_path.h"

#include "wc_db.h"


typedef svn_error_t (*walker_func_t)(const char *path,
                                     void *baton,
                                     apr_pool_t *scratch_pool);

typedef enum walker_mode_e {
    walker_mode_base,
    walker_mode_working,
    walker_mode_actual

    /* ### other variants? e.g. actual - ignored?
     * ### NO: insert a filtering callback to proxy to your callback
     */
} walker_mode_t;

struct walker_entry {
    const char *dirpath;
    const char *name;
};


/* NOTE: children should live at least as long as "pool" */
void append_entries(apr_array_header_t *queue,
                    const char *dirpath,
                    const apr_array_header_t *children,
                    apr_pool_t *pool)
{
    int i;

    for (i = 0; i < children->nelts; ++i)
      {
        const char *name = APR_ARRAY_IDX(children, i, const char *);
        struct walker_entry *entry = apr_palloc(pool, sizeof(*entry));

        entry->dirpath = dirpath;
        entry->name = name;

        APR_ARRAY_PUSH(queue, struct walker_entry *) = entry;
      }
}


svn_error_t *
generic_walker(svn_wc__db_t *db,
               const char *path,
               walker_mode_t mode,
               walker_func_t walk_func,
               void *walk_baton,
               apr_pool_t *scratch_pool)
{
    apr_pool_t *queue_pool = scratch_pool;
    apr_pool_t *iterpool = svn_pool_create(scratch_pool);
    apr_array_header_t *queue = apr_array_make(queue_pool, 0, 10);
    struct walker_entry *entry = apr_palloc(queue_pool, sizeof(*entry));

    entry->dirpath = path;
    entry->name = "";
    APR_ARRAY_PUSH(queue, struct walker_entry *) = entry;

    while (queue->nelts > 0)
      {
        const char *nodepath;
        svn_wc__db_kind_t kind;

        svn_pool_clear(iterpool);

        /* pull entries off the end of the queue */
        entry = APR_ARRAY_IDX(queue, queue->nelts - 1, struct walker_entry *);

        nodepath = svn_path_join(entry->dirpath, entry->name, iterpool);

        if (mode == walker_mode_base)
          {
            SVN_ERR(svn_wc__db_base_get_info(&kind, NULL, NULL, NULL, NULL,
                                             db, nodepath, iterpool, iterpool));
          }
        else if (mode == walker_mode_working)
          {
            SVN_ERR(svn_wc__db_read_info(&kind, NULL, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL, NULL, NULL, NULL,
                                         db, nodepath, iterpool, iterpool));
          }
        else if (mode == walker_mode_actual)
          {
          }
        else
          {
            /* ### ERROR */
          }

        if (kind == svn_wc__db_kind_dir)
          {
            const apr_array_header_t *children;

            /* copy the path into a long-lived pool */
            const char *dirpath = apr_pstrdup(queue_pool, nodepath);

            if (mode == walker_mode_base)
              {
                SVN_ERR(svn_wc__db_base_get_children(&children, db, nodepath,
                                                     queue_pool, iterpool));
              }
            else if (mode == walker_mode_working)
              {
                SVN_ERR(svn_wc__db_read_children(&children, db, nodepath,
                                                 queue_pool, iterpool));
              }
            else if (mode == walker_mode_actual)
              {
              }
            else
              {
                /* ### ERROR */
              }

            append_entries(queue, dirpath, children, queue_pool);
          }

        (*walk_func)(nodepath, walk_baton, iterpool);
      }

    svn_pool_destroy(iterpool);
    return NULL;
}
