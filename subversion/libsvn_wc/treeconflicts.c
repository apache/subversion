/*
 * treeconflicts.c: Handling of known problematic tree conflict use cases.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#include "treeconflicts.h"
#include "svn_path.h"
#include "svn_private_config.h"

struct tree_conflict_phrases
{
  const char *update_deleted;
  const char *update_edited;
  const char *merge_deleted;
  const char *merge_edited;
  const char *we_deleted;
  const char *we_edited;
  const char *does_not_exist;
};

/* Return a new (possibly localised)
 * tree_conflict_phrases object allocated in POOL. */
static struct tree_conflict_phrases *
new_tree_conflict_phrases(apr_pool_t *pool)
{
  struct tree_conflict_phrases *phrases =
    apr_palloc(pool, sizeof(struct tree_conflict_phrases));

  phrases->update_deleted = _("The update wants to delete the file '%s'\n"
                              "(possibly as part of a rename operation).\n");

  phrases->update_edited = _("The update wants to edit the file '%s'.\n");

  phrases->merge_deleted = _("The merge wants to delete the file '%s'\n"
                             "(possibly as part of a rename operation).\n");

  phrases->merge_edited = _("The merge wants to edit the file '%s'.\n");

  phrases->we_deleted = _("You have deleted '%s' locally.\n"
                          "Maybe you renamed it?\n");

  phrases->we_edited = _("You have edited '%s' locally.\n");

  phrases->does_not_exist = _("The file '%s' does not exist locally\n"
                              "Maybe you renamed it?\n");
  return phrases;
}

static const char *
select_their_phrase(svn_wc_conflict_description_t *conflict,
                    struct tree_conflict_phrases *phrases)
{
  if (conflict->operation == svn_wc_operation_update)
    {
      switch (conflict->action)
        {
          case svn_wc_conflict_action_delete:
            return phrases->update_deleted;
          case svn_wc_conflict_action_edit:
            return phrases->update_edited;
          default:
            return NULL;
        }
    }
  else if (conflict->operation == svn_wc_operation_merge)
    {
      switch (conflict->action)
        {
          case svn_wc_conflict_action_delete:
            return phrases->merge_deleted;
          case svn_wc_conflict_action_edit:
            return phrases->merge_edited;
          default:
            return NULL;
        }
    }
}

static const char *
select_our_phrase(svn_wc_conflict_description_t *conflict,
                  struct tree_conflict_phrases *phrases)
{
  switch (conflict->reason)
    {
      case svn_wc_conflict_reason_deleted:
        return phrases->we_deleted;
      case svn_wc_conflict_reason_edited:
        return phrases->we_edited;
      default:
        return NULL;
    }
}

/*
 * Transform a svn_wc_conflict_description_t tree CONFLICT
 * into a human readable description. Return the description
 * in RESULT. Allocate RESULT from POOL.
 */
svn_error_t *
svn_wc__create_tree_conflict_desc(svn_wc_conflict_description_t *conflict,
                                  svn_string_t **result,
                                  apr_pool_t *pool)
{

}

/*
 * Read tree conflict descriptions from DIR_ENTRY.
 * Return a newly allocated array of svn_wc_conflict_description_t
 * items in *RESULT. If there are no tree conflicts rooted at DIR_ENTRY,
 * set *RESULT to NULL. Do all allocations in POOL.
 */
static svn_error_t *
read_tree_conflict_entry(svn_wc_entry_t *dir_entry,
                         apr_array_header_t **result,
                         apr_pool_t *pool)
{

}

/*
 * Write tree conflict descriptions (svn_wc_conflict_description_t)
 * in DESCRIPTIONS to DIR_ENTRY.
 */
static svn_error_t *
write_tree_conflict_entry(svn_wc_entry_t *entry,
                          apr_array_header_t *descriptions)
{

}

svn_error_t *
svn_wc__add_tree_conflict_data(svn_wc_conflict_description_t *conflict,
                               svn_wc_adm_access_t *adm_access,
                               apr_pool_t *pool)
{
  const char *dir_path;
  const svn_wc_entry_t *entry;

  /* Retrieve node path from adm_access. */

  /* Make sure node is a directory. */

  /* If there is already a tree conflict victim with the same name,
   * we were called even though the update should have skipped an
   * already tree conflicted directory. We really should not be
   * here.
   */

  /* Add new tree conflict to the list of tree conflicts for node path. */

  /* Loggy write tree conflict list to entry. */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__is_tree_conflict_victim(svn_boolean_t *tree_conflict_victim,
                                const char *path,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool)
{
  /* Retrieve node path from adm_access. */

  /* Check whether node is a directory. If not, throw an error. */

  /* Get the entry for the directory. */

  /* If there is already a tree conflict victim with the same path
   * as the one we got as an argument, set tree_conflict_victim to
   * true, else set it to false */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__write_tree_conflict_descs(svn_wc_adm_access_t *adm_access,
                                  apr_pool_t *pool)
{
  /* Important: We want this function to be idempotent. */

  /* Retrieve node path from adm_access. */

  /* Check whether node is a directory. If not, throw an error. */

  /* Get tree conflict descriptions from the dir entry. */

  /* Write conflict descriptions obtained from
   * svn_wc__create_tree_conflict_desc()
   * to a new temp reject file.
   */

  /* Loggy move the temp file to the user-visible reject file path. */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__tree_conflict_resolved(const char* victim_path,
                               svn_wc_adm_access_t *adm_access,
                               apr_pool_t *pool)
{
  /* Retrieve node path from adm_access. */

  /* Check whether node is a directory. If not, throw an error. */

  /* Get tree conflict descriptions from the dir entry. */

  /* Remove the tree conflict description for victim. */

  /* If the victim list is now empty, loggy remove the reject file. */
  /* Else call svn_wc__write_tree_conflict_descs(). */

  return SVN_NO_ERROR;
}

