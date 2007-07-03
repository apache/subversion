/*
 * conflict-callbacks.c: conflict resolution callbacks specific to the
 * commandline client.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

/* ==================================================================== */



/*** Includes. ***/

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_cmdline.h"
#include "svn_client.h"
#include "cl.h"

#include "svn_private_config.h"




/* Utility to print a full description of the conflict. */
static svn_error_t *
print_conflict_description(const svn_wc_conflict_description_t *desc,
                           apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_printf(pool, _("Path: %s\n"), desc->path));
  switch (desc->node_kind)
  {
    case svn_node_file:
      SVN_ERR(svn_cmdline_printf(pool, _("Node kind: file\n")));
      SVN_ERR(svn_cmdline_printf(pool, _("Binary file?: %s\n"),
                                 desc->is_binary ? "yes" : "no"));
      if (desc->mime_type)
        SVN_ERR(svn_cmdline_printf(pool, _("Mime-type: %s"),
                                   desc->mime_type));
      break;
    case svn_node_dir:
      SVN_ERR(svn_cmdline_printf(pool, _("Node kind: directory\n")));
      break;
    default:
      SVN_ERR(svn_cmdline_printf(pool, _("Node kind: unknown\n")));
  }

  switch (desc->action)
  {
    case svn_wc_conflict_action_edit:
      SVN_ERR(svn_cmdline_printf(pool, _("Attempting to edit object.\n")));
      break;
    case svn_wc_conflict_action_add:
      SVN_ERR(svn_cmdline_printf(pool, _("Attempting to add object.\n")));
      break;
    case svn_wc_conflict_action_delete:
      SVN_ERR(svn_cmdline_printf(pool, _("Attempting to delete object.\n")));
      break;
    default:
      SVN_ERR(svn_cmdline_printf(pool, _("No action specified!\n")));
      break;
  }

  SVN_ERR(svn_cmdline_printf(pool, _("But:  ")));
  switch (desc->reason)
  {
    case svn_wc_conflict_reason_edited:
      SVN_ERR(svn_cmdline_printf(pool,
                                _("existing object has conflicting edits.\n")));
      break;
    case svn_wc_conflict_reason_obstructed:
      SVN_ERR(svn_cmdline_printf(pool, _("existing object is in the way.\n")));
      break;
    case svn_wc_conflict_reason_deleted:
      SVN_ERR(svn_cmdline_printf(pool, _("existing object is deleted.\n")));
      break;
    case svn_wc_conflict_reason_missing:
      SVN_ERR(svn_cmdline_printf(pool, _("existing object is missing.\n")));
      break;
    case svn_wc_conflict_reason_unversioned:
      SVN_ERR(svn_cmdline_printf(pool, _("existing object is unversioned.\n")));
      break;
   default:
      SVN_ERR(svn_cmdline_printf(pool, _("No reason specified!\n")));
      break;
  }

  if (desc->base_file)
    SVN_ERR(svn_cmdline_printf(pool, _("  Ancestor file: %s\n"),
                               desc->base_file));
  if (desc->repos_file)
    SVN_ERR(svn_cmdline_printf(pool, _("  Repository's file: %s\n"),
                               desc->repos_file));
  if (desc->edited_file)
    SVN_ERR(svn_cmdline_printf(pool, _("  User's file: %s\n"),
                               desc->edited_file));
  if (desc->conflict_file)
    SVN_ERR(svn_cmdline_printf(pool, _("  File with conflict markers: %s\n"),
                               desc->conflict_file));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_cl__ignore_conflicts(const svn_wc_conflict_description_t *description,
                         void *baton,
                         apr_pool_t *pool)
{
  /* This routine is still useful for debugging purposes; it makes for
     a nice breakpoint where one can examine the conflict
     description.  Or, just uncomment this bit:  */

  /*
  SVN_ERR(svn_cmdline_printf(pool, _("Discovered a conflict.\n\n")));
  SVN_ERR(print_conflict_description(description, pool));
  SVN_ERR(svn_cmdline_printf(pool, _("\n\n")));
  */

  return svn_error_create(SVN_ERR_CLIENT_CONFLICT_REMAINS, NULL,
                          _("Conflict was not resolved."));
}
