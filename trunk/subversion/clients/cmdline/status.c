/*
 * status.c:  the command-line's portion of the "svn status" command
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

/* ==================================================================== */



/*** Includes. ***/
#include "svn_cmdline.h"
#include "svn_wc.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_time.h"
#include "svn_utf.h"
#include "svn_ebcdic.h"
#include "cl.h"
#include "svn_private_config.h"


#define AUTHOR_STR \
        "\x61\x75\x74\x68\x6f\x72"
        /* "author" */

#define COMMENT_STR \
        "\x63\x6f\x6d\x6d\x65\x6e\x74"
        /* "comment" */

#define COMMIT_STR \
        "\x63\x6f\x6d\x6d\x69\x74"
        /* "commit" */

#define COPIED_STR \
        "\x63\x6f\x70\x69\x65\x64"
        /* "copied" */

#define CREATED_STR \
        "\x63\x72\x65\x61\x74\x65\x64"
        /* "created" */

#define DATE_STR \
        "\x64\x61\x74\x65"
        /* "date" */

#define ENTRY_STR \
        "\x65\x6e\x74\x72\x79"
        /* "entry" */

#define EXPIRES_STR \
        "\x65\x78\x70\x69\x72\x65\x73"
        /* "expires" */

#define ITEM_STR \
        "\x69\x74\x65\x6d"
        /* "item" */

#define LOCK_STR \
        "\x6c\x6f\x63\x6b"
        /* "lock" */

#define OWNER_STR \
        "\x6f\x77\x6e\x65\x72"
        /* "owner" */

#define PATH_STR \
        "\x70\x61\x74\x68"
        /* "path" */

#define PROPS_STR \
        "\x70\x72\x6f\x70\x73"
        /* "props" */

#define REPOS_STATUS_STR \
        "\x72\x65\x70\x6f\x73\x2d\x73\x74\x61\x74\x75\x73"
        /* "repos-status" */

#define REVISION_STR \
        "\x72\x65\x76\x69\x73\x69\x6f\x6e"
        /* "revision" */

#define SWITCHED_STR \
        "\x73\x77\x69\x74\x63\x68\x65\x64"
        /* "switched" */

#define TOKEN_STR \
        "\x74\x6f\x6b\x65\x6e"
        /* "token" */

#define TRUE_STR \
        "\x74\x72\x75\x65"
        /* "true" */

#define WC_LOCKED_STR \
        "\x77\x63\x2d\x6c\x6f\x63\x6b\x65\x64"
        /* "wc-locked" */

#define WC_STATUS_STR \
        "\x77\x63\x2d\x73\x74\x61\x74\x75\x73"
        /* "wc-status" */

/* Return the single character representation of STATUS */
static char
generate_status_code (enum svn_wc_status_kind status)
{
  switch (status)
    {
    case svn_wc_status_none:        return SVN_UTF8_SPACE;
    case svn_wc_status_normal:      return SVN_UTF8_SPACE;
    case svn_wc_status_added:       return SVN_UTF8_A;
    case svn_wc_status_missing:     return SVN_UTF8_EXCLAMATION;
    case svn_wc_status_incomplete:  return SVN_UTF8_EXCLAMATION;
    case svn_wc_status_deleted:     return SVN_UTF8_D;
    case svn_wc_status_replaced:    return SVN_UTF8_R;
    case svn_wc_status_modified:    return SVN_UTF8_M;
    case svn_wc_status_merged:      return SVN_UTF8_G;
    case svn_wc_status_conflicted:  return SVN_UTF8_C;
    case svn_wc_status_obstructed:  return SVN_UTF8_TILDE;
    case svn_wc_status_ignored:     return SVN_UTF8_I;
    case svn_wc_status_external:    return SVN_UTF8_X;
    case svn_wc_status_unversioned: return SVN_UTF8_QUESTION;
    default:                        return SVN_UTF8_QUESTION;
    }
}


/* Return the detailed string representation of STATUS */
static const char *
generate_status_desc (enum svn_wc_status_kind status)
{
  switch (status)
    {
    case svn_wc_status_none:
      return "\x6e\x6f\x6e\x65";
             /* "none" */

    case svn_wc_status_normal:
      return "\x6e\x6f\x72\x6d\x61\x6c";
             /* "normal" */

    case svn_wc_status_added:
      return "\x61\x64\x64\x65\x64";
             /* "added" */

    case svn_wc_status_missing:
      return "\x6d\x69\x73\x73\x69\x6e\x67";
             /* "missing" */

    case svn_wc_status_incomplete:
      return "\x69\x6e\x63\x6f\x6d\x70\x6c\x65\x74\x65";
             /* "incomplete" */

    case svn_wc_status_deleted:
      return "\x64\x65\x6c\x65\x74\x65\x64";
             /* "deleted" */

    case svn_wc_status_replaced:
      return "\x72\x65\x70\x6c\x61\x63\x65\x64";
             /* "replaced" */

    case svn_wc_status_modified:
      return "\x6d\x6f\x64\x69\x66\x69\x65\x64";
             /* "modified" */

    case svn_wc_status_merged:
      return "\x6d\x65\x72\x67\x65\x64";
             /* "merged" */

    case svn_wc_status_conflicted:
      return "\x63\x6f\x6e\x66\x6c\x69\x63\x74\x65\x64";
             /* "conflicted" */

    case svn_wc_status_obstructed:
      return "\x6f\x62\x73\x74\x72\x75\x63\x74\x65\x64";
             /* "obstructed" */

    case svn_wc_status_ignored:
      return "\x69\x67\x6e\x6f\x72\x65\x64";
             /* "ignored" */

    case svn_wc_status_external:
      return "\x65\x78\x74\x65\x72\x6e\x61\x6c";
             /* "external" */

    case svn_wc_status_unversioned:
      return "\x75\x6e\x76\x65\x72\x73\x69\x6f\x6e\x65\x64";
             /* "unversioned" */

    default:                        abort();
    }
}


/* Print STATUS and PATH in a format determined by DETAILED and
   SHOW_LAST_COMMITTED. */
static svn_error_t *
print_status (const char *path,
              svn_boolean_t detailed,
              svn_boolean_t show_last_committed,
              svn_boolean_t repos_locks,
              svn_wc_status2_t *status,
              apr_pool_t *pool)
{
  if (detailed)
    {
      char ood_status, lock_status;
      const char *working_rev;

      if (! status->entry)
        working_rev = "";
      else if (! SVN_IS_VALID_REVNUM (status->entry->revision))
        working_rev = SVN_UTF8_SPACE_STR \
                      SVN_UTF8_QUESTION_STR \
                      SVN_UTF8_SPACE_STR;
      else if (status->copied)
        working_rev = SVN_UTF8_MINUS_STR;
      else
        working_rev = APR_PSPRINTF2 (pool, "%ld", status->entry->revision);

      if (status->repos_text_status != svn_wc_status_none
          || status->repos_prop_status != svn_wc_status_none)
        ood_status = SVN_UTF8_ASTERISK;
      else
        ood_status = SVN_UTF8_SPACE;

      if (repos_locks)
        {
          if (status->repos_lock)
            {
              if (status->entry && status->entry->lock_token)
                {
                  if (strcmp (status->repos_lock->token, status->entry->lock_token)
                      == 0)
                    lock_status = SVN_UTF8_K;
                  else
                    lock_status = SVN_UTF8_T;
                }
              else
                lock_status = SVN_UTF8_O;
            }
          else if (status->entry && status->entry->lock_token)
            lock_status = SVN_UTF8_B;
          else
            lock_status = SVN_UTF8_SPACE;
        }
      else
        lock_status = (status->entry && status->entry->lock_token)
                      ? SVN_UTF8_K : SVN_UTF8_SPACE;

      if (show_last_committed)
        {
          const char *commit_rev;
          const char *commit_author;

          if (status->entry && SVN_IS_VALID_REVNUM (status->entry->cmt_rev))
            commit_rev = APR_PSPRINTF2(pool, "%ld", status->entry->cmt_rev);
          else if (status->entry)
            commit_rev = SVN_UTF8_SPACE_STR \
                         SVN_UTF8_QUESTION_STR \
                         SVN_UTF8_SPACE_STR;
          else
            commit_rev = "";

          if (status->entry && status->entry->cmt_author)
            commit_author = status->entry->cmt_author;
          else if (status->entry)
            commit_author = SVN_UTF8_SPACE_STR \
                            SVN_UTF8_QUESTION_STR \
                            SVN_UTF8_SPACE_STR;
          else
            commit_author = "";

          SVN_ERR
            (SVN_CMDLINE_PRINTF2 (pool,
                                  "%c%c%c%c%c%c %c   %6s   %6s %-12s %s\n",
                                  generate_status_code (status->text_status),
                                  generate_status_code (status->prop_status),
                                  status->locked ? SVN_UTF8_L : SVN_UTF8_SPACE,
                                  status->copied ? SVN_UTF8_PLUS : SVN_UTF8_SPACE,
                                  status->switched ? SVN_UTF8_S : SVN_UTF8_SPACE,
                                  lock_status,
                                  ood_status,
                                  working_rev,
                                  commit_rev,
                                  commit_author,
                                  path));
        }
      else
        SVN_ERR
          (SVN_CMDLINE_PRINTF2 (pool, "%c%c%c%c%c%c %c   %6s   %s\n",
                                generate_status_code (status->text_status),
                                generate_status_code (status->prop_status),
                                status->locked ? SVN_UTF8_L : SVN_UTF8_SPACE,
                                status->copied ? SVN_UTF8_PLUS : SVN_UTF8_SPACE,
                                status->switched ? SVN_UTF8_S : SVN_UTF8_SPACE,
                                lock_status,
                                ood_status,
                                working_rev,
                                path));
    }
  else
    SVN_ERR
      (SVN_CMDLINE_PRINTF2 (pool, "%c%c%c%c%c%c %s\n",
                            generate_status_code (status->text_status),
                            generate_status_code (status->prop_status),
                            status->locked ? SVN_UTF8_L : SVN_UTF8_SPACE,
                            status->copied ? SVN_UTF8_PLUS : SVN_UTF8_SPACE,
                            status->switched ? SVN_UTF8_S : SVN_UTF8_SPACE,
                            ((status->entry && status->entry->lock_token)
                             ? SVN_UTF8_K : SVN_UTF8_SPACE),
                            path));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__print_status_xml (const char *path,
                          svn_wc_status2_t *status,
                          apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);
  apr_hash_t *att_hash;

  if (status->text_status == svn_wc_status_none
      && status->repos_text_status == svn_wc_status_none)
    return SVN_NO_ERROR;

  svn_xml_make_open_tag (&sb, pool, svn_xml_normal, ENTRY_STR,
                         PATH_STR, svn_path_local_style (path, pool), NULL);

  att_hash = apr_hash_make (pool);
  apr_hash_set (att_hash, ITEM_STR, APR_HASH_KEY_STRING,
                generate_status_desc (status->text_status));
  apr_hash_set (att_hash, PROPS_STR, APR_HASH_KEY_STRING,
                generate_status_desc (status->prop_status));
  if (status->locked)
    apr_hash_set (att_hash, WC_LOCKED_STR, APR_HASH_KEY_STRING, TRUE_STR);
  if (status->copied)
    apr_hash_set (att_hash, COPIED_STR, APR_HASH_KEY_STRING, TRUE_STR);
  if (status->switched)
    apr_hash_set (att_hash, SWITCHED_STR, APR_HASH_KEY_STRING, TRUE_STR);
  if (status->entry && ! status->entry->copied)
    apr_hash_set (att_hash, REVISION_STR, APR_HASH_KEY_STRING,
                  APR_PSPRINTF2 (pool, "%ld", status->entry->revision));
  svn_xml_make_open_tag_hash (&sb, pool, svn_xml_normal, WC_STATUS_STR,
                              att_hash);

  if (status->entry && SVN_IS_VALID_REVNUM (status->entry->cmt_rev))
    {
      svn_xml_make_open_tag (&sb, pool, svn_xml_normal, COMMIT_STR,
                             REVISION_STR,
                             APR_PSPRINTF2 (pool, "%ld",
                                            status->entry->cmt_rev),
                             NULL);

      svn_cl__xml_tagged_cdata (&sb, pool, AUTHOR_STR,
                                status->entry->cmt_author);

      if (status->entry->cmt_date)
        svn_cl__xml_tagged_cdata (&sb, pool, DATE_STR,
                                  svn_time_to_cstring
                                    (status->entry->cmt_date, pool));

      svn_xml_make_close_tag (&sb, pool, COMMIT_STR);
    }

  if (status->entry && status->entry->lock_token)
    {
      svn_xml_make_open_tag (&sb, pool, svn_xml_normal, LOCK_STR, NULL);

      svn_cl__xml_tagged_cdata (&sb, pool, TOKEN_STR, status->entry->lock_token);

      /* If lock_owner is NULL, assume WC is corrupt. */
      if (status->entry->lock_owner)
        svn_cl__xml_tagged_cdata (&sb, pool, OWNER_STR,
                                  status->entry->lock_owner);
      else
        return svn_error_createf (SVN_ERR_WC_CORRUPT, NULL,
                                  _("'%s' has lock token, but no lock owner"),
                                  svn_path_local_style (path, pool));

      svn_cl__xml_tagged_cdata (&sb, pool, COMMENT_STR,
                                status->entry->lock_comment);

      svn_cl__xml_tagged_cdata (&sb, pool, CREATED_STR,
                                svn_time_to_cstring
                                  (status->entry->lock_creation_date, pool));

      svn_xml_make_close_tag (&sb, pool, LOCK_STR);
    }

  svn_xml_make_close_tag (&sb, pool, WC_STATUS_STR);

  if (status->repos_text_status != svn_wc_status_none
      || status->repos_prop_status != svn_wc_status_none
      || status->repos_lock)
    {
      svn_xml_make_open_tag (&sb, pool, svn_xml_normal, REPOS_STATUS_STR,
                             ITEM_STR,
                             generate_status_desc (status->repos_text_status),
                             PROPS_STR,
                             generate_status_desc (status->repos_prop_status),
                             NULL);
      if (status->repos_lock)
        {
          svn_xml_make_open_tag (&sb, pool, svn_xml_normal, LOCK_STR, NULL);

          svn_cl__xml_tagged_cdata (&sb, pool, TOKEN_STR,
                                    status->repos_lock->token);

          svn_cl__xml_tagged_cdata (&sb, pool, OWNER_STR,
                                    status->repos_lock->owner);

          svn_cl__xml_tagged_cdata (&sb, pool, COMMENT_STR,
                                    status->repos_lock->comment);

          svn_cl__xml_tagged_cdata (&sb, pool, CREATED_STR,
                                    svn_time_to_cstring
                                      (status->repos_lock->creation_date,
                                       pool));

          if (status->repos_lock->expiration_date != 0)
            {
              svn_cl__xml_tagged_cdata (&sb, pool, EXPIRES_STR,
                                        svn_time_to_cstring
                                          (status->repos_lock->expiration_date,
                                           pool));
            }

          svn_xml_make_close_tag (&sb, pool, LOCK_STR);
        }
      svn_xml_make_close_tag (&sb, pool, REPOS_STATUS_STR);
    }

  svn_xml_make_close_tag (&sb, pool, ENTRY_STR);

  return svn_cl__error_checked_fputs (sb->data, stdout);
}

/* Called by status-cmd.c */
svn_error_t *
svn_cl__print_status (const char *path,
                      svn_wc_status2_t *status,
                      svn_boolean_t detailed,
                      svn_boolean_t show_last_committed,
                      svn_boolean_t skip_unrecognized,
                      svn_boolean_t repos_locks,
                      apr_pool_t *pool)
{
  if (! status 
      || (skip_unrecognized && ! status->entry)
      || (status->text_status == svn_wc_status_none
          && status->repos_text_status == svn_wc_status_none))
    return SVN_NO_ERROR;

  return print_status (svn_path_local_style (path, pool),
                       detailed, show_last_committed, repos_locks, status,
                       pool);
}
