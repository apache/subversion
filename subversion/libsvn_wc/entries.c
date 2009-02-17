/*
 * entries.c :  manipulating the administrative `entries' file.
 *
 * ====================================================================
 * Copyright (c) 2000-2009 CollabNet.  All rights reserved.
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


#include <string.h>

#include <apr_strings.h>

#include "svn_xml.h"
#include "svn_error.h"
#include "svn_types.h"
#include "svn_time.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_ctype.h"

#include "wc.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"
#include "lock.h"
#include "tree_conflicts.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/** Overview **/

/* The administrative `entries' file tracks information about files
   and subdirs within a particular directory.

   See the section on the `entries' file in libsvn_wc/README, for
   concrete information about the XML format.
*/


/*--------------------------------------------------------------- */


/*** reading and writing the entries file ***/


static svn_wc_entry_t *
alloc_entry(apr_pool_t *pool)
{
  svn_wc_entry_t *entry = apr_pcalloc(pool, sizeof(*entry));
  entry->revision = SVN_INVALID_REVNUM;
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  entry->cmt_rev = SVN_INVALID_REVNUM;
  entry->kind = svn_node_none;
  entry->working_size = SVN_WC_ENTRY_WORKING_SIZE_UNKNOWN;
  entry->depth = svn_depth_infinity;
  entry->file_external_path = NULL;
  entry->file_external_peg_rev.kind = svn_opt_revision_unspecified;
  entry->file_external_rev.kind = svn_opt_revision_unspecified;
  return entry;
}


/* If attribute ATTR_NAME appears in hash ATTS, set *ENTRY_FLAG to its
 * boolean value and add MODIFY_FLAG into *MODIFY_FLAGS, else set *ENTRY_FLAG
 * false.  ENTRY_NAME is the name of the WC-entry. */
static svn_error_t *
do_bool_attr(svn_boolean_t *entry_flag,
             apr_uint64_t *modify_flags, apr_uint64_t modify_flag,
             apr_hash_t *atts, const char *attr_name,
             const char *entry_name)
{
  const char *str = apr_hash_get(atts, attr_name, APR_HASH_KEY_STRING);

  *entry_flag = FALSE;
  if (str)
    {
      if (strcmp(str, "true") == 0)
        *entry_flag = TRUE;
      else if (strcmp(str, "false") == 0 || strcmp(str, "") == 0)
        *entry_flag = FALSE;
      else
        return svn_error_createf
          (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
           _("Entry '%s' has invalid '%s' value"),
           (entry_name ? entry_name : SVN_WC_ENTRY_THIS_DIR), attr_name);

      *modify_flags |= modify_flag;
    }
  return SVN_NO_ERROR;
}

/* Read an escaped byte on the form 'xHH' from [*BUF, END), placing
   the byte in *RESULT.  Advance *BUF to point after the escape
   sequence. */
static svn_error_t *
read_escaped(char *result, char **buf, const char *end)
{
  apr_uint64_t val;
  char digits[3];

  if (end - *buf < 3 || **buf != 'x' || ! svn_ctype_isxdigit((*buf)[1])
      || ! svn_ctype_isxdigit((*buf)[2]))
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Invalid escape sequence"));
  (*buf)++;
  digits[0] = *((*buf)++);
  digits[1] = *((*buf)++);
  digits[2] = 0;
  if ((val = apr_strtoi64(digits, NULL, 16)) == 0)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Invalid escaped character"));
  *result = (char) val;
  return SVN_NO_ERROR;
}

/* Read a field, possibly with escaped bytes, from [*BUF, END),
   stopping at the terminator.  Place the read string in *RESULT, or set
   *RESULT to NULL if it is the empty string.  Allocate the returned string
   in POOL.  Advance *BUF to point after the terminator. */
static svn_error_t *
read_str(const char **result,
         char **buf, const char *end,
         apr_pool_t *pool)
{
  svn_stringbuf_t *s = NULL;
  const char *start;
  if (*buf == end)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Unexpected end of entry"));
  if (**buf == '\n')
    {
      *result = NULL;
      (*buf)++;
      return SVN_NO_ERROR;
    }

  start = *buf;
  while (*buf != end && **buf != '\n')
    {
      if (**buf == '\\')
        {
          char c;
          if (! s)
            s = svn_stringbuf_ncreate(start, *buf - start, pool);
          else
            svn_stringbuf_appendbytes(s, start, *buf - start);
          (*buf)++;
          SVN_ERR(read_escaped(&c, buf, end));
          svn_stringbuf_appendbytes(s, &c, 1);
          start = *buf;
        }
      else
        (*buf)++;
    }

  if (*buf == end)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Unexpected end of entry"));

  if (s)
    {
      svn_stringbuf_appendbytes(s, start, *buf - start);
      *result = s->data;
    }
  else
    *result = apr_pstrndup(pool, start, *buf - start);
  (*buf)++;
  return SVN_NO_ERROR;
}

/* This is wrapper around read_str() (which see for details); it
   simply asks svn_path_is_canonical() of the string it reads,
   returning an error if the test fails. */
static svn_error_t *
read_path(const char **result,
          char **buf, const char *end,
          apr_pool_t *pool)
{
  SVN_ERR(read_str(result, buf, end, pool));
  if (*result && **result && (! svn_path_is_canonical(*result, pool)))
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("Entry contains non-canonical path '%s'"),
                             *result);
  return SVN_NO_ERROR;
}

/* This is read_path() for urls. This function does not do the is_canonical
   test for entries from working copies older than version 10, as since that
   version the canonicalization of urls has been changed. See issue #2475.
   If the test is done and fails, read_url returs an error. */
static svn_error_t *
read_url(const char **result,
         char **buf, const char *end,
         int wc_format,
         apr_pool_t *pool)
{
  SVN_ERR(read_str(result, buf, end, pool));

  /* If the wc format is <10 canonicalize the url, */
  if (*result && **result)
    {
      if (wc_format < SVN_WC__CHANGED_CANONICAL_URLS)
        *result = svn_path_canonicalize(*result, pool);
      else
        if (! svn_path_is_canonical(*result, pool))
          return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                   _("Entry contains non-canonical path '%s'"),
                                   *result);
    }
  return SVN_NO_ERROR;
}

/* Read a field from [*BUF, END), terminated by a newline character.
   The field may not contain escape sequences.  The field is not
   copied and the buffer is modified in place, by replacing the
   terminator with a NUL byte.  Make *BUF point after the original
   terminator. */
static svn_error_t *
read_val(const char **result,
          char **buf, const char *end)
{
  const char *start = *buf;

  if (*buf == end)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Unexpected end of entry"));
  if (**buf == '\n')
    {
      (*buf)++;
      *result = NULL;
      return SVN_NO_ERROR;
    }

  while (*buf != end && **buf != '\n')
    (*buf)++;
  if (*buf == end)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Unexpected end of entry"));
  **buf = '\0';
  *result = start;
  (*buf)++;
  return SVN_NO_ERROR;
}

/* Read a boolean field from [*BUF, END), placing the result in
   *RESULT.  If there is no boolean value (just a terminator), it
   defaults to false.  Else, the value must match FIELD_NAME, in which
   case *RESULT will be set to true.  Advance *BUF to point after the
   terminator. */
static svn_error_t *
read_bool(svn_boolean_t *result, const char *field_name,
          char **buf, const char *end)
{
  const char *val;
  SVN_ERR(read_val(&val, buf, end));
  if (val)
    {
      if (strcmp(val, field_name) != 0)
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                 _("Invalid value for field '%s'"),
                                 field_name);
      *result = TRUE;
    }
  else
    *result = FALSE;
  return SVN_NO_ERROR;
}

/* Read a revision number from [*BUF, END) stopping at the
   terminator.  Set *RESULT to the revision number, or
   SVN_INVALID_REVNUM if there is none.  Use POOL for temporary
   allocations.  Make *BUF point after the terminator.  */
static svn_error_t *
read_revnum(svn_revnum_t *result,
            char **buf,
            const char *end,
            apr_pool_t *pool)
{
  const char *val;

  SVN_ERR(read_val(&val, buf, end));

  if (val)
    *result = SVN_STR_TO_REV(val);
  else
    *result = SVN_INVALID_REVNUM;

  return SVN_NO_ERROR;
}

/* Read a timestamp from [*BUF, END) stopping at the terminator.
   Set *RESULT to the resulting timestamp, or 0 if there is none.  Use
   POOL for temporary allocations.  Make *BUF point after the
   terminator. */
static svn_error_t *
read_time(apr_time_t *result,
          char **buf, const char *end,
          apr_pool_t *pool)
{
  const char *val;

  SVN_ERR(read_val(&val, buf, end));
  if (val)
    SVN_ERR(svn_time_from_cstring(result, val, pool));
  else
    *result = 0;

  return SVN_NO_ERROR;
}

/**
 * Parse the string at *STR as an revision and save the result in
 * *OPT_REV.  After returning successfully, *STR points at next
 * character in *STR where further parsing can be done.
 */
static svn_error_t *
string_to_opt_revision(svn_opt_revision_t *opt_rev,
                       const char **str,
                       apr_pool_t *pool)
{
  const char *s = *str;

  SVN_ERR_ASSERT(opt_rev);

  while (*s && *s != ':')
    ++s;

  /* Should not find a \0. */
  if (!*s)
    return svn_error_createf
      (SVN_ERR_INCORRECT_PARAMS, NULL,
       _("Found an unexpected \\0 in the file external '%s'"), *str);

  if (0 == strncmp(*str, "HEAD:", 5))
    {
      opt_rev->kind = svn_opt_revision_head;
    }
  else
    {
      svn_revnum_t rev;
      const char *endptr;

      SVN_ERR(svn_revnum_parse(&rev, *str, &endptr));
      SVN_ERR_ASSERT(endptr == s);
      opt_rev->kind = svn_opt_revision_number;
      opt_rev->value.number = rev;
    }

  *str = s + 1;

  return SVN_NO_ERROR;
}

/**
 * Given a revision, return a string for the revision, either "HEAD"
 * or a string representation of the revision value.  All other
 * revision kinds return an error.
 */
static svn_error_t *
opt_revision_to_string(const char **str,
                       const char *path,
                       const svn_opt_revision_t *rev,
                       apr_pool_t *pool)
{
  switch (rev->kind)
    {
    case svn_opt_revision_head:
      *str = apr_pstrmemdup(pool, "HEAD", 4);
      break;
    case svn_opt_revision_number:
      *str = apr_itoa(pool, rev->value.number);
      break;
    default:
      return svn_error_createf
        (SVN_ERR_INCORRECT_PARAMS, NULL,
         _("Illegal file external revision kind %d for path '%s'"),
         rev->kind, path);
      break;
    }

  return SVN_NO_ERROR;
}

/* Parse a file external specification in the NULL terminated STR and
   place the path in PATH_RESULT, the peg revision in PEG_REV_RESULT
   and revision number in REV_RESULT.  STR may be NULL, in which case
   PATH_RESULT will be set to NULL and both PEG_REV_RESULT and
   REV_RESULT set to svn_opt_revision_unspecified.

   The format that is read is the same as a working-copy path with a
   peg revision; see svn_opt_parse_path(). */
static svn_error_t *
unserialize_file_external(const char **path_result,
                          svn_opt_revision_t *peg_rev_result,
                          svn_opt_revision_t *rev_result,
                          const char *str,
                          apr_pool_t *pool)
{
  if (str)
    {
      svn_opt_revision_t peg_rev;
      svn_opt_revision_t op_rev;
      const char *s = str;

      SVN_ERR(string_to_opt_revision(&peg_rev, &s, pool));
      SVN_ERR(string_to_opt_revision(&op_rev, &s, pool));

      *path_result = apr_pstrdup(pool, s);
      *peg_rev_result = peg_rev;
      *rev_result = op_rev;
    }
  else
    {
      *path_result = NULL;
      peg_rev_result->kind = svn_opt_revision_unspecified;
      rev_result->kind = svn_opt_revision_unspecified;
    }

  return SVN_NO_ERROR;
}

/* Serialize into STR the file external path, peg revision number and
   the operative revision number into a format that
   unserialize_file_external() can parse.  The format is
     %{peg_rev}:%{rev}:%{path}
   where a rev will either be HEAD or the string revision number.  If
   PATH is NULL then STR will be set to NULL.  This method writes to a
   string instead of a svn_stringbuf_t so that the string can be
   protected by write_str(). */
static svn_error_t *
serialize_file_external(const char **str,
                        const char *path,
                        const svn_opt_revision_t *peg_rev,
                        const svn_opt_revision_t *rev,
                        apr_pool_t *pool)
{
  const char *s;

  if (path)
    {
      const char *s1;
      const char *s2;

      SVN_ERR(opt_revision_to_string(&s1, path, peg_rev, pool));
      SVN_ERR(opt_revision_to_string(&s2, path, rev, pool));

      s = apr_pstrcat(pool, s1, ":", s2, ":", path, NULL);
    }
  else
    s = NULL;

  *str = s;

  return SVN_NO_ERROR;
}

/* Allocate an entry from POOL and read it from [*BUF, END).  The
   buffer may be modified in place while parsing.  Return the new
   entry in *NEW_ENTRY.  Advance *BUF to point at the end of the entry
   record.
   The entries file format should be provided in ENTRIES_FORMAT. */
static svn_error_t *
read_entry(svn_wc_entry_t **new_entry,
           char **buf, const char *end,
           int entries_format,
           apr_pool_t *pool)
{
  svn_wc_entry_t *entry = alloc_entry(pool);
  const char *name;

#define MAYBE_DONE if (**buf == '\f') goto done

  /* Find the name and set up the entry under that name. */
  SVN_ERR(read_path(&name, buf, end, pool));
  entry->name = name ? name : SVN_WC_ENTRY_THIS_DIR;

  /* Set up kind. */
  {
    const char *kindstr;
    SVN_ERR(read_val(&kindstr, buf, end));
    if (kindstr)
      {
        if (strcmp(kindstr, SVN_WC__ENTRIES_ATTR_FILE_STR) == 0)
          entry->kind = svn_node_file;
        else if (strcmp(kindstr, SVN_WC__ENTRIES_ATTR_DIR_STR) == 0)
          entry->kind = svn_node_dir;
        else
          return svn_error_createf
            (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
             _("Entry '%s' has invalid node kind"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR));
      }
    else
      entry->kind = svn_node_none;
  }
  MAYBE_DONE;

  /* Attempt to set revision (resolve_to_defaults may do it later, too) */
  SVN_ERR(read_revnum(&entry->revision, buf, end, pool));
  MAYBE_DONE;

  /* Attempt to set up url path (again, see resolve_to_defaults). */
  SVN_ERR(read_url(&entry->url, buf, end, entries_format, pool));
  MAYBE_DONE;

  /* Set up repository root.  Make sure it is a prefix of url. */
  SVN_ERR(read_url(&entry->repos, buf, end, entries_format, pool));
  if (entry->repos && entry->url
      && ! svn_path_is_ancestor(entry->repos, entry->url))
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("Entry for '%s' has invalid repository "
                               "root"),
                             name ? name : SVN_WC_ENTRY_THIS_DIR);
  MAYBE_DONE;

  /* Look for a schedule attribute on this entry. */
  {
    const char *schedulestr;
    SVN_ERR(read_val(&schedulestr, buf, end));
    entry->schedule = svn_wc_schedule_normal;
    if (schedulestr)
      {
        if (strcmp(schedulestr, SVN_WC__ENTRY_VALUE_ADD) == 0)
          entry->schedule = svn_wc_schedule_add;
        else if (strcmp(schedulestr, SVN_WC__ENTRY_VALUE_DELETE) == 0)
          entry->schedule = svn_wc_schedule_delete;
        else if (strcmp(schedulestr, SVN_WC__ENTRY_VALUE_REPLACE) == 0)
          entry->schedule = svn_wc_schedule_replace;
        else
          return svn_error_createf
            (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
             _("Entry '%s' has invalid '%s' value"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR),
             SVN_WC__ENTRY_ATTR_SCHEDULE);
      }
  }
  MAYBE_DONE;

  /* Attempt to set up text timestamp. */
  SVN_ERR(read_time(&entry->text_time, buf, end, pool));
  MAYBE_DONE;

  /* Checksum. */
  SVN_ERR(read_str(&entry->checksum, buf, end, pool));
  MAYBE_DONE;

  /* Setup last-committed values. */
  SVN_ERR(read_time(&entry->cmt_date, buf, end, pool));
  MAYBE_DONE;

  SVN_ERR(read_revnum(&entry->cmt_rev, buf, end, pool));
  MAYBE_DONE;

  SVN_ERR(read_str(&entry->cmt_author, buf, end, pool));
  MAYBE_DONE;

  /* has-props flag. */
  SVN_ERR(read_bool(&entry->has_props, SVN_WC__ENTRY_ATTR_HAS_PROPS,
                    buf, end));
  MAYBE_DONE;

  /* has-prop-mods flag. */
  SVN_ERR(read_bool(&entry->has_prop_mods, SVN_WC__ENTRY_ATTR_HAS_PROP_MODS,
                    buf, end));
  MAYBE_DONE;

  /* cachable-props string. */
  {
    const char *unused_value;
    SVN_ERR(read_val(&unused_value, buf, end));

    /* Fill in a value for the (deprecated) cachable_props. Note that if
       we're reading from a current-format 'entries', then present_props
       will have been computed properly. */
    entry->cachable_props = SVN_WC__CACHABLE_PROPS;
  }
  MAYBE_DONE;

  /* present-props string. */
  SVN_ERR(read_val(&entry->present_props, buf, end));
  if (entry->present_props)
    entry->present_props = apr_pstrdup(pool, entry->present_props);
  MAYBE_DONE;

  /* Is this entry in a state of mental torment (conflict)? */
  {
    SVN_ERR(read_path(&entry->prejfile, buf, end, pool));
    MAYBE_DONE;
    SVN_ERR(read_path(&entry->conflict_old, buf, end, pool));
    MAYBE_DONE;
    SVN_ERR(read_path(&entry->conflict_new, buf, end, pool));
    MAYBE_DONE;
    SVN_ERR(read_path(&entry->conflict_wrk, buf, end, pool));
    MAYBE_DONE;
  }

  /* Is this entry copied? */
  SVN_ERR(read_bool(&entry->copied, SVN_WC__ENTRY_ATTR_COPIED, buf, end));
  MAYBE_DONE;

  SVN_ERR(read_url(&entry->copyfrom_url, buf, end, entries_format, pool));
  MAYBE_DONE;
  SVN_ERR(read_revnum(&entry->copyfrom_rev, buf, end, pool));
  MAYBE_DONE;

  /* Is this entry deleted? */
  SVN_ERR(read_bool(&entry->deleted, SVN_WC__ENTRY_ATTR_DELETED, buf, end));
  MAYBE_DONE;

  /* Is this entry absent? */
  SVN_ERR(read_bool(&entry->absent, SVN_WC__ENTRY_ATTR_ABSENT, buf, end));
  MAYBE_DONE;

  /* Is this entry incomplete? */
  SVN_ERR(read_bool(&entry->incomplete, SVN_WC__ENTRY_ATTR_INCOMPLETE,
                    buf, end));
  MAYBE_DONE;

  /* UUID. */
  SVN_ERR(read_str(&entry->uuid, buf, end, pool));
  MAYBE_DONE;

  /* Lock token. */
  SVN_ERR(read_str(&entry->lock_token, buf, end, pool));
  MAYBE_DONE;

  /* Lock owner. */
  SVN_ERR(read_str(&entry->lock_owner, buf, end, pool));
  MAYBE_DONE;

  /* Lock comment. */
  SVN_ERR(read_str(&entry->lock_comment, buf, end, pool));
  MAYBE_DONE;

  /* Lock creation date. */
  SVN_ERR(read_time(&entry->lock_creation_date, buf, end, pool));
  MAYBE_DONE;

  /* Changelist. */
  SVN_ERR(read_str(&entry->changelist, buf, end, pool));
  MAYBE_DONE;

  /* Keep entry in working copy after deletion? */
  SVN_ERR(read_bool(&entry->keep_local, SVN_WC__ENTRY_ATTR_KEEP_LOCAL,
                    buf, end));
  MAYBE_DONE;

  /* Translated size */
  {
    const char *val;

    /* read_val() returns NULL on an empty (e.g. default) entry line,
       and entry has already been initialized accordingly already */
    SVN_ERR(read_val(&val, buf, end));
    if (val)
      entry->working_size = (apr_off_t)apr_strtoi64(val, NULL, 0);
  }
  MAYBE_DONE;

  /* Depth. */
  {
    const char *result;
    SVN_ERR(read_val(&result, buf, end));
    if (result)
      {
        svn_boolean_t invalid;
        svn_boolean_t is_this_dir;

        entry->depth = svn_depth_from_word(result);

        /* Verify the depth value:
           THIS_DIR should not have an excluded value and SUB_DIR should only
           have excluded value. Remember that infinity value is not stored and
           should not show up here. Otherwise, something bad may have
           happened. However, infinity value itself will always be okay. */
        is_this_dir = !name;
        /* '!=': XOR */
        invalid = is_this_dir != (entry->depth != svn_depth_exclude);
        if (entry->depth != svn_depth_infinity && invalid)
          return svn_error_createf
            (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
             _("Entry '%s' has invalid depth"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR));
      }
    else
      entry->depth = svn_depth_infinity;

  }
  MAYBE_DONE;

  /* Tree conflict data. */
  SVN_ERR(read_str(&entry->tree_conflict_data, buf, end, pool));
  MAYBE_DONE;

  /* File external URL and revision. */
  {
    const char *str;
    SVN_ERR(read_str(&str, buf, end, pool));
    SVN_ERR(unserialize_file_external(&entry->file_external_path,
                                      &entry->file_external_peg_rev,
                                      &entry->file_external_rev,
                                      str,
                                      pool));
  }
  MAYBE_DONE;

 done:
  *new_entry = entry;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__atts_to_entry(svn_wc_entry_t **new_entry,
                      apr_uint64_t *modify_flags,
                      apr_hash_t *atts,
                      apr_pool_t *pool)
{
  svn_wc_entry_t *entry = alloc_entry(pool);
  const char *name;

  *modify_flags = 0;

  /* Find the name and set up the entry under that name. */
  name = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_NAME, APR_HASH_KEY_STRING);
  entry->name = name ? apr_pstrdup(pool, name) : SVN_WC_ENTRY_THIS_DIR;

  /* Attempt to set revision (resolve_to_defaults may do it later, too) */
  {
    const char *revision_str
      = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_REVISION, APR_HASH_KEY_STRING);

    if (revision_str)
      {
        entry->revision = SVN_STR_TO_REV(revision_str);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_REVISION;
      }
    else
      entry->revision = SVN_INVALID_REVNUM;
  }

  /* Attempt to set up url path (again, see resolve_to_defaults). */
  {
    entry->url
      = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_URL, APR_HASH_KEY_STRING);

    if (entry->url)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_URL;
        entry->url = apr_pstrdup(pool, entry->url);
      }
  }

  /* Set up repository root.  Make sure it is a prefix of url. */
  {
    entry->repos = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_REPOS,
                                APR_HASH_KEY_STRING);
    if (entry->repos)
      {
        if (entry->url && ! svn_path_is_ancestor(entry->repos, entry->url))
          return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                   _("Entry for '%s' has invalid repository "
                                     "root"),
                                   name ? name : SVN_WC_ENTRY_THIS_DIR);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_REPOS;
        entry->repos = apr_pstrdup(pool, entry->repos);
      }
  }

  /* Set up kind. */
  {
    const char *kindstr
      = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_KIND, APR_HASH_KEY_STRING);

    entry->kind = svn_node_none;
    if (kindstr)
      {
        if (strcmp(kindstr, SVN_WC__ENTRIES_ATTR_FILE_STR) == 0)
          entry->kind = svn_node_file;
        else if (strcmp(kindstr, SVN_WC__ENTRIES_ATTR_DIR_STR) == 0)
          entry->kind = svn_node_dir;
        else
          return svn_error_createf
            (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
             _("Entry '%s' has invalid node kind"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR));
        *modify_flags |= SVN_WC__ENTRY_MODIFY_KIND;
      }
  }

  /* Look for a schedule attribute on this entry. */
  {
    const char *schedulestr
      = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_SCHEDULE, APR_HASH_KEY_STRING);

    entry->schedule = svn_wc_schedule_normal;
    if (schedulestr)
      {
        if (strcmp(schedulestr, SVN_WC__ENTRY_VALUE_ADD) == 0)
          entry->schedule = svn_wc_schedule_add;
        else if (strcmp(schedulestr, SVN_WC__ENTRY_VALUE_DELETE) == 0)
          entry->schedule = svn_wc_schedule_delete;
        else if (strcmp(schedulestr, SVN_WC__ENTRY_VALUE_REPLACE) == 0)
          entry->schedule = svn_wc_schedule_replace;
        else if (strcmp(schedulestr, "") == 0)
          entry->schedule = svn_wc_schedule_normal;
        else
          return svn_error_createf
            (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
             _("Entry '%s' has invalid '%s' value"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR),
             SVN_WC__ENTRY_ATTR_SCHEDULE);

        *modify_flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE;
      }
  }

  /* Is this entry in a state of mental torment (conflict)? */
  {
    if ((entry->prejfile
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_PREJFILE,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_PREJFILE;
        /* Normalize "" (used by the log runner) to NULL */
        entry->prejfile = *(entry->prejfile)
          ? apr_pstrdup(pool, entry->prejfile) : NULL;
      }

    if ((entry->conflict_old
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CONFLICT_OLD,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_OLD;
        /* Normalize "" (used by the log runner) to NULL */
        entry->conflict_old =
          *(entry->conflict_old)
          ? apr_pstrdup(pool, entry->conflict_old) : NULL;
      }

    if ((entry->conflict_new
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CONFLICT_NEW,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_NEW;
        /* Normalize "" (used by the log runner) to NULL */
        entry->conflict_new =
          *(entry->conflict_new)
          ? apr_pstrdup(pool, entry->conflict_new) : NULL;
      }

    if ((entry->conflict_wrk
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CONFLICT_WRK,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_WRK;
        /* Normalize "" (used by the log runner) to NULL */
        entry->conflict_wrk =
          *(entry->conflict_wrk)
          ? apr_pstrdup(pool, entry->conflict_wrk) : NULL;
      }

    if ((entry->tree_conflict_data
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_TREE_CONFLICT_DATA,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_TREE_CONFLICT_DATA;
        /* Normalize "" (used by the log runner) to NULL */
        entry->tree_conflict_data =
          *(entry->tree_conflict_data)
          ? apr_pstrdup(pool, entry->tree_conflict_data) : NULL;
      }
  }

  /* Is this entry copied? */
  SVN_ERR(do_bool_attr(&entry->copied,
                       modify_flags, SVN_WC__ENTRY_MODIFY_COPIED,
                       atts, SVN_WC__ENTRY_ATTR_COPIED, name));
  {
    const char *revstr;

    entry->copyfrom_url = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_COPYFROM_URL,
                                       APR_HASH_KEY_STRING);
    if (entry->copyfrom_url)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_URL;
        entry->copyfrom_url = apr_pstrdup(pool, entry->copyfrom_url);
      }

    revstr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_COPYFROM_REV,
                          APR_HASH_KEY_STRING);
    if (revstr)
      {
        entry->copyfrom_rev = SVN_STR_TO_REV(revstr);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_REV;
      }
  }

  /* Is this entry deleted? */
  SVN_ERR(do_bool_attr(&entry->deleted,
                       modify_flags, SVN_WC__ENTRY_MODIFY_DELETED,
                       atts, SVN_WC__ENTRY_ATTR_DELETED, name));

  /* Is this entry absent? */
  SVN_ERR(do_bool_attr(&entry->absent,
                       modify_flags, SVN_WC__ENTRY_MODIFY_ABSENT,
                       atts, SVN_WC__ENTRY_ATTR_ABSENT, name));

  /* Is this entry incomplete? */
  SVN_ERR(do_bool_attr(&entry->incomplete,
                       modify_flags, SVN_WC__ENTRY_MODIFY_INCOMPLETE,
                       atts, SVN_WC__ENTRY_ATTR_INCOMPLETE, name));

  /* Should this item be kept in the working copy after deletion? */
  SVN_ERR(do_bool_attr(&entry->keep_local,
                       modify_flags, SVN_WC__ENTRY_MODIFY_KEEP_LOCAL,
                       atts, SVN_WC__ENTRY_ATTR_KEEP_LOCAL, name));

  /* Attempt to set up timestamps. */
  {
    const char *text_timestr;

    text_timestr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_TEXT_TIME,
                                APR_HASH_KEY_STRING);
    if (text_timestr)
      {
        if (strcmp(text_timestr, SVN_WC__TIMESTAMP_WC) == 0)
          {
            /* Special case:  a magic string that means 'get this value
               from the working copy' -- we ignore it here, trusting
               that the caller of this function know what to do about
               it.  */
          }
        else
          SVN_ERR(svn_time_from_cstring(&entry->text_time, text_timestr,
                                        pool));

        *modify_flags |= SVN_WC__ENTRY_MODIFY_TEXT_TIME;
      }

    /* Note: we do not persist prop_time, so there is no need to attempt
       to parse a new prop_time value from the log. Certainly, on any
       recent working copy, there will not be a log record to alter
       the prop_time value. */
  }

  /* Checksum. */
  {
    entry->checksum = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CHECKSUM,
                                   APR_HASH_KEY_STRING);
    if (entry->checksum)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CHECKSUM;
        entry->checksum = apr_pstrdup(pool, entry->checksum);
      }
  }

  /* UUID. */
  {
    entry->uuid = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_UUID,
                               APR_HASH_KEY_STRING);
    if (entry->uuid)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_UUID;
        entry->uuid = apr_pstrdup(pool, entry->uuid);
      }
  }

  /* Setup last-committed values. */
  {
    const char *cmt_datestr, *cmt_revstr;

    cmt_datestr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CMT_DATE,
                               APR_HASH_KEY_STRING);
    if (cmt_datestr)
      {
        SVN_ERR(svn_time_from_cstring(&entry->cmt_date, cmt_datestr, pool));
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_DATE;
      }
    else
      entry->cmt_date = 0;

    cmt_revstr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CMT_REV,
                              APR_HASH_KEY_STRING);
    if (cmt_revstr)
      {
        entry->cmt_rev = SVN_STR_TO_REV(cmt_revstr);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_REV;
      }
    else
      entry->cmt_rev = SVN_INVALID_REVNUM;

    entry->cmt_author = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CMT_AUTHOR,
                                     APR_HASH_KEY_STRING);
    if (entry->cmt_author)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_AUTHOR;
        entry->cmt_author = apr_pstrdup(pool, entry->cmt_author);
      }
  }

  /* Lock token. */
  entry->lock_token = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_LOCK_TOKEN,
                                   APR_HASH_KEY_STRING);
  if (entry->lock_token)
    {
      *modify_flags |= SVN_WC__ENTRY_MODIFY_LOCK_TOKEN;
      entry->lock_token = apr_pstrdup(pool, entry->lock_token);
    }

  /* lock owner. */
  entry->lock_owner = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_LOCK_OWNER,
                                   APR_HASH_KEY_STRING);
  if (entry->lock_owner)
    {
      *modify_flags |= SVN_WC__ENTRY_MODIFY_LOCK_OWNER;
      entry->lock_owner = apr_pstrdup(pool, entry->lock_owner);
    }

  /* lock comment. */
  entry->lock_comment = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_LOCK_COMMENT,
                                     APR_HASH_KEY_STRING);
  if (entry->lock_comment)
    {
      *modify_flags |= SVN_WC__ENTRY_MODIFY_LOCK_COMMENT;
      entry->lock_comment = apr_pstrdup(pool, entry->lock_comment);
    }

  /* lock creation date. */
  {
    const char *cdate_str =
      apr_hash_get(atts, SVN_WC__ENTRY_ATTR_LOCK_CREATION_DATE,
                   APR_HASH_KEY_STRING);
    if (cdate_str)
      {
        SVN_ERR(svn_time_from_cstring(&entry->lock_creation_date,
                                      cdate_str, pool));
        *modify_flags |= SVN_WC__ENTRY_MODIFY_LOCK_CREATION_DATE;
      }
  }

  /* has-props flag. */
  SVN_ERR(do_bool_attr(&entry->has_props,
                       modify_flags, SVN_WC__ENTRY_MODIFY_HAS_PROPS,
                       atts, SVN_WC__ENTRY_ATTR_HAS_PROPS, name));

  /* has-prop-mods flag. */
  {
    const char *has_prop_mods_str
      = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_HAS_PROP_MODS,
                     APR_HASH_KEY_STRING);

    if (has_prop_mods_str)
      {
        if (strcmp(has_prop_mods_str, "true") == 0)
          entry->has_prop_mods = TRUE;
        else if (strcmp(has_prop_mods_str, "false") != 0)
          return svn_error_createf
            (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
             _("Entry '%s' has invalid '%s' value"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR),
             SVN_WC__ENTRY_ATTR_HAS_PROP_MODS);

        *modify_flags |= SVN_WC__ENTRY_MODIFY_HAS_PROP_MODS;
      }
  }

  /* NOTE: if there is an attribute for the (deprecated) cachable_props,
     then we're just going to ignore it. */

  /* present-props string. */
  entry->present_props = apr_hash_get(atts,
                                      SVN_WC__ENTRY_ATTR_PRESENT_PROPS,
                                      APR_HASH_KEY_STRING);
  if (entry->present_props)
    {
      *modify_flags |= SVN_WC__ENTRY_MODIFY_PRESENT_PROPS;
      entry->present_props = apr_pstrdup(pool, entry->present_props);
    }

  /* Translated size */
  {
    const char *val
      = apr_hash_get(atts,
                     SVN_WC__ENTRY_ATTR_WORKING_SIZE,
                     APR_HASH_KEY_STRING);
    if (val)
      {
        if (strcmp(val, SVN_WC__WORKING_SIZE_WC) == 0)
          {
            /* Special case (same as the timestamps); ignore here
               these will be handled elsewhere */
          }
        else
          /* Cast to off_t; it's safe: we put in an off_t to start with... */
          entry->working_size = (apr_off_t)apr_strtoi64(val, NULL, 0);

        *modify_flags |= SVN_WC__ENTRY_MODIFY_WORKING_SIZE;
      }
  }

  *new_entry = entry;
  return SVN_NO_ERROR;
}

/* Used when reading an entries file in XML format. */
struct entries_accumulator
{
  /* Keys are entry names, vals are (struct svn_wc_entry_t *)'s. */
  apr_hash_t *entries;

  /* The parser that's parsing it, for signal_expat_bailout(). */
  svn_xml_parser_t *parser;

  /* Don't leave home without one. */
  apr_pool_t *pool;

  /* Cleared before handling each entry. */
  apr_pool_t *scratch_pool;
};


/* Is the entry in a 'hidden' state in the sense of the 'show_hidden'
 * switches on svn_wc_entries_read(), svn_wc_walk_entries*(), etc.? */
static svn_boolean_t
entry_is_hidden(const svn_wc_entry_t *entry)
{
  return ((entry->deleted && entry->schedule != svn_wc_schedule_add)
          || entry->absent);
}


/* Called whenever we find an <open> tag of some kind. */
static void
handle_start_tag(void *userData, const char *tagname, const char **atts)
{
  struct entries_accumulator *accum = userData;
  apr_hash_t *attributes;
  svn_wc_entry_t *entry;
  svn_error_t *err;
  apr_uint64_t modify_flags = 0;

  /* We only care about the `entry' tag; all other tags, such as `xml'
     and `wc-entries', are ignored. */
  if (strcmp(tagname, SVN_WC__ENTRIES_ENTRY))
    return;

  svn_pool_clear(accum->scratch_pool);
  /* Make an entry from the attributes. */
  attributes = svn_xml_make_att_hash(atts, accum->scratch_pool);
  err = svn_wc__atts_to_entry(&entry, &modify_flags, attributes, accum->pool);
  if (err)
    {
      svn_xml_signal_bailout(err, accum->parser);
      return;
    }

  /* Find the name and set up the entry under that name.  This
     should *NOT* be NULL, since svn_wc__atts_to_entry() should
     have made it into SVN_WC_ENTRY_THIS_DIR. */
  apr_hash_set(accum->entries, entry->name, APR_HASH_KEY_STRING, entry);
}

/* Parse BUF of size SIZE as an entries file in XML format, storing the parsed
   entries in ENTRIES.  Use pool for temporary allocations and the pool of
   ADM_ACCESS for the returned entries. */
static svn_error_t *
parse_entries_xml(svn_wc_adm_access_t *adm_access,
                  apr_hash_t *entries,
                  const char *buf,
                  apr_size_t size,
                  apr_pool_t *pool)
{
  svn_xml_parser_t *svn_parser;
  struct entries_accumulator accum;

  /* Set up userData for the XML parser. */
  accum.entries = entries;
  accum.pool = svn_wc_adm_access_pool(adm_access);
  accum.scratch_pool = svn_pool_create(pool);

  /* Create the XML parser */
  svn_parser = svn_xml_make_parser(&accum,
                                   handle_start_tag,
                                   NULL,
                                   NULL,
                                   pool);

  /* Store parser in its own userdata, so callbacks can call
     svn_xml_signal_bailout() */
  accum.parser = svn_parser;

  /* Parse. */
  SVN_ERR_W(svn_xml_parse(svn_parser, buf, size, TRUE),
            apr_psprintf(pool,
                         _("XML parser failed in '%s'"),
                         svn_path_local_style
                         (svn_wc_adm_access_path(adm_access), pool)));

  svn_pool_destroy(accum.scratch_pool);

  /* Clean up the XML parser */
  svn_xml_free_parser(svn_parser);

  return SVN_NO_ERROR;
}



/* Use entry SRC to fill in blank portions of entry DST.  SRC itself
   may not have any blanks, of course.
   Typically, SRC is a parent directory's own entry, and DST is some
   child in that directory. */
static void
take_from_entry(svn_wc_entry_t *src, svn_wc_entry_t *dst, apr_pool_t *pool)
{
  /* Inherits parent's revision if doesn't have a revision of one's
     own, unless this is a subdirectory. */
  if ((dst->revision == SVN_INVALID_REVNUM) && (dst->kind != svn_node_dir))
    dst->revision = src->revision;

  /* Inherits parent's url if doesn't have a url of one's own. */
  if (! dst->url)
    dst->url = svn_path_url_add_component2(src->url, dst->name, pool);

  if (! dst->repos)
    dst->repos = src->repos;

  if ((! dst->uuid)
      && (! ((dst->schedule == svn_wc_schedule_add)
             || (dst->schedule == svn_wc_schedule_replace))))
    {
      dst->uuid = src->uuid;
    }
}


/* Resolve any missing information in ENTRIES by deducing from the
   directory's own entry (which must already be present in ENTRIES). */
static svn_error_t *
resolve_to_defaults(apr_hash_t *entries,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_wc_entry_t *default_entry
    = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);

  /* First check the dir's own entry for consistency. */
  if (! default_entry)
    return svn_error_create(SVN_ERR_ENTRY_NOT_FOUND,
                            NULL,
                            _("Missing default entry"));

  if (default_entry->revision == SVN_INVALID_REVNUM)
    return svn_error_create(SVN_ERR_ENTRY_MISSING_REVISION,
                            NULL,
                            _("Default entry has no revision number"));

  if (! default_entry->url)
    return svn_error_create(SVN_ERR_ENTRY_MISSING_URL,
                            NULL,
                            _("Default entry is missing URL"));


  /* Then use it to fill in missing information in other entries. */
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      void *val;
      svn_wc_entry_t *this_entry;

      apr_hash_this(hi, NULL, NULL, &val);
      this_entry = val;

      if (this_entry == default_entry)
        /* THIS_DIR already has all the information it can possibly
           have.  */
        continue;

      if (this_entry->kind == svn_node_dir)
        /* Entries that are directories have everything but their
           name, kind, and state stored in the THIS_DIR entry of the
           directory itself.  However, we are disallowing the perusing
           of any entries outside of the current entries file.  If a
           caller wants more info about a directory, it should look in
           the entries file in the directory.  */
        continue;

      if (this_entry->kind == svn_node_file)
        /* For file nodes that do not explicitly have their ancestry
           stated, this can be derived from the default entry of the
           directory in which those files reside.  */
        take_from_entry(default_entry, this_entry, pool);
    }

  return SVN_NO_ERROR;
}



/* Fill the entries cache in ADM_ACCESS. The full hash cache will be
   populated.  POOL is used for local memory allocation, the access baton
   pool is used for the cache. */
static svn_error_t *
read_entries(svn_wc_adm_access_t *adm_access,
             apr_pool_t *scratch_pool)
{
  apr_pool_t *result_pool = svn_wc_adm_access_pool(adm_access);
  const char *path = svn_wc_adm_access_path(adm_access);
  apr_hash_t *entries = apr_hash_make(result_pool);
  char *curp;
  const char *endp;
  svn_wc_entry_t *entry;
  int entryno, entries_format;
  svn_stream_t *stream;
  svn_string_t *buf;

  /* Open the entries file. */
  SVN_ERR(svn_wc__open_adm_stream(&stream, path, SVN_WC__ADM_ENTRIES,
                                  scratch_pool, scratch_pool));
  SVN_ERR(svn_string_from_stream(&buf, stream, scratch_pool, scratch_pool));

  /* We own the returned data; it is modifiable, so cast away... */
  curp = (char *)buf->data;
  endp = buf->data + buf->len;

  /* If the first byte of the file is not a digit, then it is probably in XML
     format. */
  if (curp != endp && !svn_ctype_isdigit(*curp))
    SVN_ERR(parse_entries_xml(adm_access, entries, buf->data, buf->len,
                              scratch_pool));
  else
    {
      const char *val;

      /* Read the format line from the entries file. In case we're in the
         middle of upgrading a working copy, this line will contain the
         original format pre-upgrade. */
      SVN_ERR(read_val(&val, &curp, endp));
      if (val)
        entries_format = (apr_off_t)apr_strtoi64(val, NULL, 0);
      else
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                 _("Invalid version line in entries file "
                                   "of '%s'"),
                                 svn_path_local_style(path, scratch_pool));
      entryno = 1;

      while (curp != endp)
        {
          svn_error_t *err = read_entry(&entry, &curp, endp,
                                        entries_format, result_pool);
          if (! err)
            {
              /* We allow extra fields at the end of the line, for
                 extensibility. */
              curp = memchr(curp, '\f', endp - curp);
              if (! curp)
                err = svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                                       _("Missing entry terminator"));
              if (! err && (curp == endp || *(++curp) != '\n'))
                err = svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                                       _("Invalid entry terminator"));
            }
          if (err)
            return svn_error_createf(err->apr_err, err,
                                     _("Error at entry %d in entries file for "
                                       "'%s':"),
                                     entryno,
                                     svn_path_local_style(path, scratch_pool));

          ++curp;
          ++entryno;

          apr_hash_set(entries, entry->name, APR_HASH_KEY_STRING, entry);
        }
    }

  /* Fill in any implied fields. */
  SVN_ERR(resolve_to_defaults(entries, result_pool));

  svn_wc__adm_access_set_entries(adm_access, TRUE, entries);

  return SVN_NO_ERROR;
}

/* For non-directory PATHs full entry information is obtained by reading
 * the entries for the parent directory of PATH and then extracting PATH's
 * entry.  If PATH is a directory then only abrieviated information is
 * available in the parent directory, more complete information is
 * available by reading the entries for PATH itself.
 *
 * Note: There is one bit of information about directories that is only
 * available in the parent directory, that is the "deleted" state.  If PATH
 * is a versioned directory then the "deleted" state information will not
 * be returned in ENTRY.  This means some bits of the code (e.g. revert)
 * need to obtain it by directly extracting the directory entry from the
 * parent directory's entries.  I wonder if this function should handle
 * that?
 */
svn_error_t *
svn_wc_entry(const svn_wc_entry_t **entry,
             const char *path,
             svn_wc_adm_access_t *adm_access,
             svn_boolean_t show_hidden,
             apr_pool_t *pool)
{
  const char *entry_name;
  svn_wc_adm_access_t *dir_access;

  SVN_ERR(svn_wc__adm_retrieve_internal(&dir_access, adm_access, path, pool));
  if (! dir_access)
    {
      const char *dir_path, *base_name;
      svn_path_split(path, &dir_path, &base_name, pool);
      SVN_ERR(svn_wc__adm_retrieve_internal(&dir_access, adm_access, dir_path,
                                            pool));
      entry_name = base_name;
    }
  else
    entry_name = SVN_WC_ENTRY_THIS_DIR;

  if (dir_access)
    {
      apr_hash_t *entries;
      SVN_ERR(svn_wc_entries_read(&entries, dir_access, show_hidden, pool));
      *entry = apr_hash_get(entries, entry_name, APR_HASH_KEY_STRING);
    }
  else
    *entry = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entry_versioned_internal(const svn_wc_entry_t **entry,
                                 const char *path,
                                 svn_wc_adm_access_t *adm_access,
                                 svn_boolean_t show_hidden,
                                 const char *caller_filename,
                                 int caller_lineno,
                                 apr_pool_t *pool)
{
  SVN_ERR(svn_wc_entry(entry, path, adm_access, show_hidden, pool));

  if (! *entry)
    {
      svn_error_t *err
        = svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                            _("'%s' is not under version control"),
                            svn_path_local_style(path, pool));

      err->file = caller_filename;
      err->line = caller_lineno;
      return err;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_entries_read(apr_hash_t **entries,
                    svn_wc_adm_access_t *adm_access,
                    svn_boolean_t show_hidden,
                    apr_pool_t *pool)
{
  apr_hash_t *new_entries;

  new_entries = svn_wc__adm_access_entries(adm_access, show_hidden, pool);
  if (! new_entries)
    {
      /* Ask for the deleted entries because most operations request them
         at some stage, getting them now avoids a second file parse. */
      SVN_ERR(read_entries(adm_access, pool));

      new_entries = svn_wc__adm_access_entries(adm_access, show_hidden, pool);
    }

  *entries = new_entries;
  return SVN_NO_ERROR;
}

/* If STR is non-null, append STR to BUF, terminating it with a
   newline, escaping bytes that needs escaping, using POOL for
   temporary allocations.  Else if STR is null, just append the
   terminating newline. */
static void
write_str(svn_stringbuf_t *buf, const char *str, apr_pool_t *pool)
{
  const char *start = str;
  if (str)
    {
      while (*str)
        {
          /* Escape control characters and | and \. */
          if (svn_ctype_iscntrl(*str) || *str == '\\')
            {
              svn_stringbuf_appendbytes(buf, start, str - start);
              svn_stringbuf_appendcstr(buf,
                                       apr_psprintf(pool, "\\x%02x", *str));
              start = str + 1;
            }
          ++str;
        }
      svn_stringbuf_appendbytes(buf, start, str - start);
    }
  svn_stringbuf_appendbytes(buf, "\n", 1);
}

/* Append the string VAL of length LEN to BUF, without escaping any
   bytes, followed by a terminator.  If VAL is NULL, ignore LEN and
   append just the terminator. */
static void
write_val(svn_stringbuf_t *buf, const char *val, apr_size_t len)
{
  if (val)
    svn_stringbuf_appendbytes(buf, val, len);
  svn_stringbuf_appendbytes(buf, "\n", 1);
}

/* If VAL is true, append FIELD_NAME followed by a terminator to BUF.
   Else, just append the terminator. */
static void
write_bool(svn_stringbuf_t *buf, const char *field_name, svn_boolean_t val)
{
  write_val(buf, val ? field_name : NULL, val ? strlen(field_name) : 0);
}

/* If REVNUM is valid, append the representation of REVNUM to BUF
   followed by a terminator, using POOL for temporary allocations.
   Otherwise, just append the terminator. */
static void
write_revnum(svn_stringbuf_t *buf, svn_revnum_t revnum, apr_pool_t *pool)
{
  if (SVN_IS_VALID_REVNUM(revnum))
    svn_stringbuf_appendcstr(buf, apr_ltoa(pool, revnum));
  svn_stringbuf_appendbytes(buf, "\n", 1);
}

/* Append the timestamp VAL to BUF (or the empty string if VAL is 0),
   followed by a terminator.  Use POOL for temporary allocations. */
static void
write_time(svn_stringbuf_t *buf, apr_time_t val, apr_pool_t *pool)
{
  if (val)
    svn_stringbuf_appendcstr(buf, svn_time_to_cstring(val, pool));
  svn_stringbuf_appendbytes(buf, "\n", 1);
}

/* Append a single entry ENTRY to the string OUTPUT, using the
   entry for "this dir" THIS_DIR for comparison/optimization.
   Allocations are done in POOL.  */
static svn_error_t *
write_entry(svn_stringbuf_t *buf,
            const svn_wc_entry_t *entry,
            const char *name,
            const svn_wc_entry_t *this_dir,
            apr_pool_t *pool)
{
  const char *valuestr;
  svn_revnum_t valuerev;
  svn_boolean_t is_this_dir = strcmp(name, SVN_WC_ENTRY_THIS_DIR) == 0;
  svn_boolean_t is_subdir = ! is_this_dir && (entry->kind == svn_node_dir);

  SVN_ERR_ASSERT(name);

  /* Name. */
  write_str(buf, name, pool);

  /* Kind. */
  switch (entry->kind)
    {
    case svn_node_dir:
      write_val(buf, SVN_WC__ENTRIES_ATTR_DIR_STR,
                 sizeof(SVN_WC__ENTRIES_ATTR_DIR_STR) - 1);
      break;

    case svn_node_none:
      write_val(buf, NULL, 0);
      break;

    case svn_node_file:
    case svn_node_unknown:
    default:
      write_val(buf, SVN_WC__ENTRIES_ATTR_FILE_STR,
                 sizeof(SVN_WC__ENTRIES_ATTR_FILE_STR) - 1);
      break;
    }

  /* Revision. */
  if (is_this_dir || (! is_subdir && entry->revision != this_dir->revision))
    valuerev = entry->revision;
  else
    valuerev = SVN_INVALID_REVNUM;
  write_revnum(buf, valuerev, pool);

  /* URL. */
  if (is_this_dir ||
      (! is_subdir && strcmp(svn_path_url_add_component2(this_dir->url, name,
                                                         pool),
                             entry->url) != 0))
    valuestr = entry->url;
  else
    valuestr = NULL;
  write_str(buf, valuestr, pool);

  /* Repository root. */
  if (! is_subdir
      && (is_this_dir
          || (this_dir->repos == NULL
              || (entry->repos
                  && strcmp(this_dir->repos, entry->repos) != 0))))
    valuestr = entry->repos;
  else
    valuestr = NULL;
  write_str(buf, valuestr, pool);

  /* Schedule. */
  switch (entry->schedule)
    {
    case svn_wc_schedule_add:
      write_val(buf, SVN_WC__ENTRY_VALUE_ADD,
                 sizeof(SVN_WC__ENTRY_VALUE_ADD) - 1);
      break;

    case svn_wc_schedule_delete:
      write_val(buf, SVN_WC__ENTRY_VALUE_DELETE,
                 sizeof(SVN_WC__ENTRY_VALUE_DELETE) - 1);
      break;

    case svn_wc_schedule_replace:
      write_val(buf, SVN_WC__ENTRY_VALUE_REPLACE,
                 sizeof(SVN_WC__ENTRY_VALUE_REPLACE) - 1);
      break;

    case svn_wc_schedule_normal:
    default:
      write_val(buf, NULL, 0);
      break;
    }

  /* Text time. */
  write_time(buf, entry->text_time, pool);

  /* Checksum. */
  write_val(buf, entry->checksum,
             entry->checksum ? strlen(entry->checksum) : 0);

  /* Last-commit stuff */
  write_time(buf, entry->cmt_date, pool);
  write_revnum(buf, entry->cmt_rev, pool);
  write_str(buf, entry->cmt_author, pool);

  /* has-props flag. */
  write_bool(buf, SVN_WC__ENTRY_ATTR_HAS_PROPS, entry->has_props);

  /* has-prop-mods flag. */
  write_bool(buf, SVN_WC__ENTRY_ATTR_HAS_PROP_MODS, entry->has_prop_mods);

  /* cachable-props string. Deprecated, so write nothing. */
  write_val(buf, NULL, 0);

  /* present-props string. */
  write_val(buf, entry->present_props,
             entry->present_props ? strlen(entry->present_props) : 0);

  /* Conflict. */
  write_str(buf, entry->prejfile, pool);
  write_str(buf, entry->conflict_old, pool);
  write_str(buf, entry->conflict_new, pool);
  write_str(buf, entry->conflict_wrk, pool);

  write_bool(buf, SVN_WC__ENTRY_ATTR_COPIED, entry->copied);

  /* Copy-related Stuff */
  write_str(buf, entry->copyfrom_url, pool);
  write_revnum(buf, entry->copyfrom_rev, pool);

  /* Deleted state */
  write_bool(buf, SVN_WC__ENTRY_ATTR_DELETED, entry->deleted);

  /* Absent state */
  write_bool(buf, SVN_WC__ENTRY_ATTR_ABSENT, entry->absent);

  /* Incomplete state */
  write_bool(buf, SVN_WC__ENTRY_ATTR_INCOMPLETE, entry->incomplete);

  /* UUID. */
  if (is_this_dir || ! this_dir->uuid || ! entry->uuid
      || strcmp(this_dir->uuid, entry->uuid) != 0)
    valuestr = entry->uuid;
  else
    valuestr = NULL;
  write_val(buf, valuestr, valuestr ? strlen(valuestr) : 0);

  /* Lock token. */
  write_str(buf, entry->lock_token, pool);

  /* Lock owner. */
  write_str(buf, entry->lock_owner, pool);

  /* Lock comment. */
  write_str(buf, entry->lock_comment, pool);

  /* Lock creation date. */
  write_time(buf, entry->lock_creation_date, pool);

  /* Changelist. */
  write_str(buf, entry->changelist, pool);

  /* Keep in working copy flag. */
  write_bool(buf, SVN_WC__ENTRY_ATTR_KEEP_LOCAL, entry->keep_local);

  /* Translated size */
  {
    const char *val
      = (entry->working_size != SVN_WC_ENTRY_WORKING_SIZE_UNKNOWN)
      ? apr_off_t_toa(pool, entry->working_size) : "";
    write_val(buf, val, strlen(val));
  }

  /* Depth. */
  /* Accept `exclude' for subdir entry. */
  if ((is_subdir && entry->depth != svn_depth_exclude)
      || entry->depth == svn_depth_infinity)
    {
      write_val(buf, NULL, 0);
    }
  else
    {
      const char *val = svn_depth_to_word(entry->depth);
      write_val(buf, val, strlen(val));
    }

  /* Tree conflict data. */
  write_str(buf, entry->tree_conflict_data, pool);

  /* File externals. */
  {
    const char *s;
    SVN_ERR(serialize_file_external(&s, entry->file_external_path,
                                    &entry->file_external_peg_rev,
                                    &entry->file_external_rev, pool));
    write_str(buf, s, pool);
  }

  /* Remove redundant separators at the end of the entry. */
  while (buf->len > 1 && buf->data[buf->len - 2] == '\n')
    buf->len--;

  svn_stringbuf_appendbytes(buf, "\f\n", 2);

  return SVN_NO_ERROR;
}

/* Append a single entry ENTRY as an XML element to the string OUTPUT,
   using the entry for "this dir" THIS_DIR for
   comparison/optimization.  Allocations are done in POOL.  */
static svn_error_t *
write_entry_xml(svn_stringbuf_t **output,
                const svn_wc_entry_t *entry,
                const char *name,
                const svn_wc_entry_t *this_dir,
                apr_pool_t *pool)
{
  apr_hash_t *atts = apr_hash_make(pool);
  const char *valuestr;

  /*** Create a hash that represents an entry. ***/

  SVN_ERR_ASSERT(name);

  /* Name */
  apr_hash_set(atts, SVN_WC__ENTRY_ATTR_NAME, APR_HASH_KEY_STRING,
               entry->name);

  /* Revision */
  if (SVN_IS_VALID_REVNUM(entry->revision))
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_REVISION, APR_HASH_KEY_STRING,
                 apr_psprintf(pool, "%ld", entry->revision));

  /* URL */
  if (entry->url)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_URL, APR_HASH_KEY_STRING,
                 entry->url);

  /* Repository root */
  if (entry->repos)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_REPOS, APR_HASH_KEY_STRING,
                 entry->repos);

  /* Kind */
  switch (entry->kind)
    {
    case svn_node_dir:
      valuestr = SVN_WC__ENTRIES_ATTR_DIR_STR;
      break;

    case svn_node_none:
      valuestr = NULL;
      break;

    case svn_node_file:
    case svn_node_unknown:
    default:
      valuestr = SVN_WC__ENTRIES_ATTR_FILE_STR;
      break;
    }
  apr_hash_set(atts, SVN_WC__ENTRY_ATTR_KIND, APR_HASH_KEY_STRING, valuestr);

  /* Schedule */
  switch (entry->schedule)
    {
    case svn_wc_schedule_add:
      valuestr = SVN_WC__ENTRY_VALUE_ADD;
      break;

    case svn_wc_schedule_delete:
      valuestr = SVN_WC__ENTRY_VALUE_DELETE;
      break;

    case svn_wc_schedule_replace:
      valuestr = SVN_WC__ENTRY_VALUE_REPLACE;
      break;

    case svn_wc_schedule_normal:
    default:
      valuestr = NULL;
      break;
    }
  apr_hash_set(atts, SVN_WC__ENTRY_ATTR_SCHEDULE, APR_HASH_KEY_STRING,
               valuestr);

  /* Conflicts */
  if (entry->conflict_old)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_CONFLICT_OLD, APR_HASH_KEY_STRING,
                 entry->conflict_old);

  if (entry->conflict_new)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_CONFLICT_NEW, APR_HASH_KEY_STRING,
                 entry->conflict_new);

  if (entry->conflict_wrk)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_CONFLICT_WRK, APR_HASH_KEY_STRING,
                 entry->conflict_wrk);

  if (entry->prejfile)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_PREJFILE, APR_HASH_KEY_STRING,
                 entry->prejfile);

  /* Copy-related Stuff */
  apr_hash_set(atts, SVN_WC__ENTRY_ATTR_COPIED, APR_HASH_KEY_STRING,
               (entry->copied ? "true" : NULL));

  if (SVN_IS_VALID_REVNUM(entry->copyfrom_rev))
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_COPYFROM_REV, APR_HASH_KEY_STRING,
                 apr_psprintf(pool, "%ld",
                              entry->copyfrom_rev));

  if (entry->copyfrom_url)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_COPYFROM_URL, APR_HASH_KEY_STRING,
                 entry->copyfrom_url);

  /* Deleted state */
  apr_hash_set(atts, SVN_WC__ENTRY_ATTR_DELETED, APR_HASH_KEY_STRING,
               (entry->deleted ? "true" : NULL));

  /* Absent state */
  apr_hash_set(atts, SVN_WC__ENTRY_ATTR_ABSENT, APR_HASH_KEY_STRING,
               (entry->absent ? "true" : NULL));

  /* Incomplete state */
  apr_hash_set(atts, SVN_WC__ENTRY_ATTR_INCOMPLETE, APR_HASH_KEY_STRING,
               (entry->incomplete ? "true" : NULL));

  /* Timestamps */
  if (entry->text_time)
    {
      apr_hash_set(atts, SVN_WC__ENTRY_ATTR_TEXT_TIME, APR_HASH_KEY_STRING,
                   svn_time_to_cstring(entry->text_time, pool));
    }
  /* Note: prop_time is no longer stored in "entries", so there is no need
     to persist it into XML either. */

  /* Checksum */
  if (entry->checksum)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_CHECKSUM, APR_HASH_KEY_STRING,
                 entry->checksum);

  /* Last-commit stuff */
  if (SVN_IS_VALID_REVNUM(entry->cmt_rev))
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_CMT_REV, APR_HASH_KEY_STRING,
                 apr_psprintf(pool, "%ld", entry->cmt_rev));

  if (entry->cmt_author)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_CMT_AUTHOR, APR_HASH_KEY_STRING,
                 entry->cmt_author);

  if (entry->uuid)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_UUID, APR_HASH_KEY_STRING,
                 entry->uuid);

  if (entry->cmt_date)
    {
      apr_hash_set(atts, SVN_WC__ENTRY_ATTR_CMT_DATE, APR_HASH_KEY_STRING,
                   svn_time_to_cstring(entry->cmt_date, pool));
    }

  /* Lock token */
  if (entry->lock_token)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_LOCK_TOKEN, APR_HASH_KEY_STRING,
                 entry->lock_token);

  /* Lock owner */
  if (entry->lock_owner)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_LOCK_OWNER, APR_HASH_KEY_STRING,
                 entry->lock_owner);

  /* Lock comment */
  if (entry->lock_comment)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_LOCK_COMMENT, APR_HASH_KEY_STRING,
                 entry->lock_comment);

  /* Lock creation date */
  if (entry->lock_creation_date)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_LOCK_CREATION_DATE,
                 APR_HASH_KEY_STRING,
                 svn_time_to_cstring(entry->lock_creation_date, pool));

  /* Has-props flag. */
  apr_hash_set(atts, SVN_WC__ENTRY_ATTR_HAS_PROPS, APR_HASH_KEY_STRING,
               (entry->has_props ? "true" : NULL));

  /* Prop-mods. */
  if (entry->has_prop_mods)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_HAS_PROP_MODS,
                 APR_HASH_KEY_STRING, "true");

  /* Cachable props. Deprecated, so do not add an attribute. */

  /* Present props. */
  if (entry->present_props
      && *entry->present_props)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_PRESENT_PROPS,
                 APR_HASH_KEY_STRING, entry->present_props);

  /* NOTE: if new entries are *added* to svn_wc_entry_t, then they do not
     have to be written here. This function is ONLY used during the "cleanup"
     phase just before we upgrade away from an XML entries file. The old
     logs will never attempt to modify new fields. */

  /*** Now, remove stuff that can be derived through inheritance rules. ***/

  /* We only want to write out 'revision' and 'url' for the
     following things:
     1. the current directory's "this dir" entry.
     2. non-directory entries:
        a. which are marked for addition (and consequently should
           have an invalid revnum)
        b. whose revision or url is valid and different than
           that of the "this dir" entry.
  */
  if (strcmp(name, SVN_WC_ENTRY_THIS_DIR))
    {
      /* This is NOT the "this dir" entry */

      SVN_ERR_ASSERT(strcmp(name, ".") != 0);
          /* By golly, if this isn't recognized as the "this dir"
             entry, and it looks like '.', we're just asking for an
             infinite recursion to happen.  Abort! */



      if (entry->kind == svn_node_dir)
        {
          /* We don't write url, revision, repository root or uuid for subdir
             entries. */
          apr_hash_set(atts, SVN_WC__ENTRY_ATTR_REVISION, APR_HASH_KEY_STRING,
                       NULL);
          apr_hash_set(atts, SVN_WC__ENTRY_ATTR_URL, APR_HASH_KEY_STRING,
                       NULL);
          apr_hash_set(atts, SVN_WC__ENTRY_ATTR_REPOS, APR_HASH_KEY_STRING,
                       NULL);
          apr_hash_set(atts, SVN_WC__ENTRY_ATTR_UUID, APR_HASH_KEY_STRING,
                       NULL);
        }
      else
        {
          /* If this is not the "this dir" entry, and the revision is
             the same as that of the "this dir" entry, don't write out
             the revision. */
          if (entry->revision == this_dir->revision)
            apr_hash_set(atts, SVN_WC__ENTRY_ATTR_REVISION,
                         APR_HASH_KEY_STRING, NULL);

          /* If this is not the "this dir" entry, and the uuid is
             the same as that of the "this dir" entry, don't write out
             the uuid. */
          if (entry->uuid && this_dir->uuid)
            {
              if (strcmp(entry->uuid, this_dir->uuid) == 0)
                apr_hash_set(atts, SVN_WC__ENTRY_ATTR_UUID,
                             APR_HASH_KEY_STRING, NULL);
            }

          /* If this is not the "this dir" entry, and the url is
             trivially calculable from that of the "this dir" entry,
             don't write out the url */
          if (entry->url)
            {
              if (strcmp(entry->url,
                         svn_path_url_add_component2(this_dir->url,
                                                     name, pool)) == 0)
                apr_hash_set(atts, SVN_WC__ENTRY_ATTR_URL,
                             APR_HASH_KEY_STRING, NULL);
            }

          /* Avoid writing repository root if that's the same as this_dir. */
          if (entry->repos && this_dir->repos
              && strcmp(entry->repos, this_dir->repos) == 0)
            apr_hash_set(atts, SVN_WC__ENTRY_ATTR_REPOS, APR_HASH_KEY_STRING,
                         NULL);
        }
    }

  /* Append the entry onto the accumulating string. */
  svn_xml_make_open_tag_hash(output,
                             pool,
                             svn_xml_self_closing,
                             SVN_WC__ENTRIES_ENTRY,
                             atts);

  return SVN_NO_ERROR;
}

/* Construct an entries file from the ENTRIES hash in XML format in a
   newly allocated stringbuf and return it in *OUTPUT.  Allocate the
   result in POOL.  THIS_DIR is the this_dir entry in ENTRIES.  */
static svn_error_t *
write_entries_xml(svn_stringbuf_t **output,
                  apr_hash_t *entries,
                  const svn_wc_entry_t *this_dir,
                  apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);

  svn_xml_make_header(output, pool);
  svn_xml_make_open_tag(output, pool, svn_xml_normal,
                        SVN_WC__ENTRIES_TOPLEVEL,
                        "xmlns",
                        SVN_XML_NAMESPACE,
                        NULL);

  /* Write out "this dir" */
  SVN_ERR(write_entry_xml(output, this_dir, SVN_WC_ENTRY_THIS_DIR,
                          this_dir, pool));

  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const svn_wc_entry_t *this_entry;

      svn_pool_clear(subpool);

      /* Get the entry and make sure its attributes are up-to-date. */
      apr_hash_this(hi, &key, NULL, &val);
      this_entry = val;

      /* Don't rewrite the "this dir" entry! */
      if (strcmp(key, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* Append the entry to output */
      SVN_ERR(write_entry_xml(output, this_entry, key, this_dir, subpool));
    }

  svn_xml_make_close_tag(output, pool, SVN_WC__ENTRIES_TOPLEVEL);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__entries_write(apr_hash_t *entries,
                      svn_wc_adm_access_t *adm_access,
                      apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_stringbuf_t *bigstr = NULL;
  svn_stream_t *stream;
  const char *temp_file_path;
  apr_hash_index_t *hi;
  const svn_wc_entry_t *this_dir;
  apr_size_t len;

  SVN_ERR(svn_wc__adm_write_check(adm_access, pool));

  /* Get a copy of the "this dir" entry for comparison purposes. */
  this_dir = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                          APR_HASH_KEY_STRING);

  /* If there is no "this dir" entry, something is wrong. */
  if (! this_dir)
    return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                             _("No default entry in directory '%s'"),
                             svn_path_local_style
                             (svn_wc_adm_access_path(adm_access), pool));

  /* Open entries file for writing.  It's important we don't use APR_EXCL
   * here.  Consider what happens if a log file is interrupted, it may
   * leave a .svn/tmp/entries file behind.  Then when cleanup reruns the
   * log file, and it attempts to modify the entries file, APR_EXCL would
   * cause an error that prevents cleanup running.  We don't use log file
   * tags such as SVN_WC__LOG_MV to move entries files so any existing file
   * is not "valuable".
   */
  SVN_ERR(svn_wc__open_adm_writable(&stream,
                                    &temp_file_path,
                                    svn_wc_adm_access_path(adm_access),
                                    SVN_WC__ADM_ENTRIES,
                                    pool, pool));

  if (svn_wc__adm_wc_format(adm_access) > SVN_WC__XML_ENTRIES_VERSION)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);

      bigstr = svn_stringbuf_createf(pool, "%d\n",
                                     svn_wc__adm_wc_format(adm_access));

      /* Write out "this dir" */
      SVN_ERR(write_entry(bigstr, this_dir, SVN_WC_ENTRY_THIS_DIR,
                          this_dir, pool));

      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const svn_wc_entry_t *this_entry;

          svn_pool_clear(iterpool);

          /* Get the entry and make sure its attributes are up-to-date. */
          apr_hash_this(hi, &key, NULL, &val);
          this_entry = val;

          /* Don't rewrite the "this dir" entry! */
          if (strcmp(key, SVN_WC_ENTRY_THIS_DIR) == 0)
            continue;

          /* Append the entry to BIGSTR */
          SVN_ERR(write_entry(bigstr, this_entry, key, this_dir, iterpool));
        }

      svn_pool_destroy(iterpool);
    }
  else
    /* This is needed during cleanup of a not yet upgraded WC. */
    SVN_ERR(write_entries_xml(&bigstr, entries, this_dir, pool));

  len = bigstr->len;
  SVN_ERR_W(svn_stream_write(stream, bigstr->data, &len),
            apr_psprintf(pool,
                         _("Error writing to '%s'"),
                         svn_path_local_style
                         (svn_wc_adm_access_path(adm_access), pool)));

  err = svn_wc__close_adm_stream(stream, temp_file_path,
                                 svn_wc_adm_access_path(adm_access),
                                 SVN_WC__ADM_ENTRIES, pool);

  svn_wc__adm_access_set_entries(adm_access, TRUE, entries);
  svn_wc__adm_access_set_entries(adm_access, FALSE, NULL);

  return err;
}


/* Update an entry NAME in ENTRIES, according to the combination of
   entry data found in ENTRY and masked by MODIFY_FLAGS. If the entry
   already exists, the requested changes will be folded (merged) into
   the entry's existing state.  If the entry doesn't exist, the entry
   will be created with exactly those properties described by the set
   of changes. Also cleanups meaningless fields combinations.

   The SVN_WC__ENTRY_MODIFY_FORCE flag is ignored.

   POOL may be used to allocate memory referenced by ENTRIES.
 */
static svn_error_t *
fold_entry(apr_hash_t *entries,
           const char *name,
           apr_uint64_t modify_flags,
           const svn_wc_entry_t *entry,
           apr_pool_t *pool)
{
  svn_wc_entry_t *cur_entry
    = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

  SVN_ERR_ASSERT(name != NULL);

  if (! cur_entry)
    cur_entry = alloc_entry(pool);

  /* Name (just a safeguard here, really) */
  if (! cur_entry->name)
    cur_entry->name = apr_pstrdup(pool, name);

  /* Revision */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_REVISION)
    cur_entry->revision = entry->revision;

  /* Ancestral URL in repository */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_URL)
    cur_entry->url = entry->url ? apr_pstrdup(pool, entry->url) : NULL;

  /* Repository root */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_REPOS)
    cur_entry->repos = entry->repos ? apr_pstrdup(pool, entry->repos) : NULL;

  /* Kind */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_KIND)
    cur_entry->kind = entry->kind;

  /* Schedule */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
    cur_entry->schedule = entry->schedule;

  /* Checksum */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CHECKSUM)
    cur_entry->checksum = entry->checksum
      ? apr_pstrdup(pool, entry->checksum)
                          : NULL;

  /* Copy-related stuff */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_COPIED)
    cur_entry->copied = entry->copied;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_COPYFROM_URL)
    cur_entry->copyfrom_url = entry->copyfrom_url
      ? apr_pstrdup(pool, entry->copyfrom_url)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_COPYFROM_REV)
    cur_entry->copyfrom_rev = entry->copyfrom_rev;

  /* Deleted state */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_DELETED)
    cur_entry->deleted = entry->deleted;

  /* Absent state */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_ABSENT)
    cur_entry->absent = entry->absent;

  /* Incomplete state */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_INCOMPLETE)
    cur_entry->incomplete = entry->incomplete;

  /* Text/prop modification times */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_TEXT_TIME)
    cur_entry->text_time = entry->text_time;

  /* Conflict stuff */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CONFLICT_OLD)
    cur_entry->conflict_old = entry->conflict_old
      ? apr_pstrdup(pool, entry->conflict_old)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CONFLICT_NEW)
    cur_entry->conflict_new = entry->conflict_new
      ? apr_pstrdup(pool, entry->conflict_new)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CONFLICT_WRK)
    cur_entry->conflict_wrk = entry->conflict_wrk
      ? apr_pstrdup(pool, entry->conflict_wrk)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_PREJFILE)
    cur_entry->prejfile = entry->prejfile
      ? apr_pstrdup(pool, entry->prejfile)
                          : NULL;

  /* Last-commit stuff */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CMT_REV)
    cur_entry->cmt_rev = entry->cmt_rev;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CMT_DATE)
    cur_entry->cmt_date = entry->cmt_date;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CMT_AUTHOR)
    cur_entry->cmt_author = entry->cmt_author
      ? apr_pstrdup(pool, entry->cmt_author)
                            : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_UUID)
    cur_entry->uuid = entry->uuid
      ? apr_pstrdup(pool, entry->uuid)
                            : NULL;

  /* Lock token */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_LOCK_TOKEN)
    cur_entry->lock_token = (entry->lock_token
                             ? apr_pstrdup(pool, entry->lock_token)
                             : NULL);

  /* Lock owner */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_LOCK_OWNER)
    cur_entry->lock_owner = (entry->lock_owner
                             ? apr_pstrdup(pool, entry->lock_owner)
                             : NULL);

  /* Lock comment */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_LOCK_COMMENT)
    cur_entry->lock_comment = (entry->lock_comment
                               ? apr_pstrdup(pool, entry->lock_comment)
                               : NULL);

  /* Lock creation date */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_LOCK_CREATION_DATE)
    cur_entry->lock_creation_date = entry->lock_creation_date;

  /* Changelist */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CHANGELIST)
    cur_entry->changelist = (entry->changelist
                             ? apr_pstrdup(pool, entry->changelist)
                             : NULL);

  /* has-props flag */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_HAS_PROPS)
    cur_entry->has_props = entry->has_props;

  /* prop-mods flag */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_HAS_PROP_MODS)
    cur_entry->has_prop_mods = entry->has_prop_mods;

  /* Cachable props. Deprecated, so we do not copy it. */

  /* Property existence */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_PRESENT_PROPS)
    cur_entry->present_props = (entry->present_props
                                ? apr_pstrdup(pool, entry->present_props)
                                : NULL);

  if (modify_flags & SVN_WC__ENTRY_MODIFY_KEEP_LOCAL)
    cur_entry->keep_local = entry->keep_local;

  /* Note that we don't bother to fold entry->depth, because it is
     only meaningful on the this-dir entry anyway. */

  /* Tree conflict data. */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_TREE_CONFLICT_DATA)
    cur_entry->tree_conflict_data = entry->tree_conflict_data
      ? apr_pstrdup(pool, entry->tree_conflict_data)
                              : NULL;

  /* Absorb defaults from the parent dir, if any, unless this is a
     subdir entry. */
  if (cur_entry->kind != svn_node_dir)
    {
      svn_wc_entry_t *default_entry
        = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
      if (default_entry)
        take_from_entry(default_entry, cur_entry, pool);
    }

  /* Cleanup meaningless fields */

  /* ### svn_wc_schedule_delete is the minimal value. We need it because it's
     impossible to NULLify copyfrom_url with log-instructions.

     Note that I tried to find the smallest collection not to clear these
     fields for, but this condition still fails the test suite:

     !(entry->schedule == svn_wc_schedule_add
       || entry->schedule == svn_wc_schedule_replace
       || (entry->schedule == svn_wc_schedule_normal && entry->copied)))

  */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE
      && entry->schedule == svn_wc_schedule_delete)
    {
      cur_entry->copied = FALSE;
      cur_entry->copyfrom_rev = SVN_INVALID_REVNUM;
      cur_entry->copyfrom_url = NULL;
    }

  if (modify_flags & SVN_WC__ENTRY_MODIFY_WORKING_SIZE)
    cur_entry->working_size = entry->working_size;

  /* keep_local makes sense only when we are going to delete directory. */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE
      && entry->schedule != svn_wc_schedule_delete)
    {
      cur_entry->keep_local = FALSE;
    }

  /* File externals. */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_FILE_EXTERNAL)
    {
      cur_entry->file_external_path = (entry->file_external_path
                                       ? apr_pstrdup(pool,
                                                     entry->file_external_path)
                                       : NULL);
      cur_entry->file_external_peg_rev = entry->file_external_peg_rev;
      cur_entry->file_external_rev = entry->file_external_rev;
    }

  /* Make sure the entry exists in the entries hash.  Possibly it
     already did, in which case this could have been skipped, but what
     the heck. */
  apr_hash_set(entries, cur_entry->name, APR_HASH_KEY_STRING, cur_entry);

  return SVN_NO_ERROR;
}


void
svn_wc__entry_remove(apr_hash_t *entries, const char *name)
{
  apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);
}


/* Our general purpose intelligence module for handling a scheduling
   change to a single entry.

   Given an entryname NAME in ENTRIES, examine the caller's requested
   scheduling change in *SCHEDULE and the current state of the entry.
   *MODIFY_FLAGS should have the 'SCHEDULE' flag set (else do nothing) and
   may have the 'FORCE' flag set (in which case do nothing).
   Determine the final schedule for the entry. Output the result by doing
   none or any or all of: delete the entry from *ENTRIES, change *SCHEDULE
   to the new schedule, remove the 'SCHEDULE' change flag from
   *MODIFY_FLAGS.

   POOL is used for local allocations only, calling this function does not
   use POOL to allocate any memory referenced by ENTRIES.
 */
static svn_error_t *
fold_scheduling(apr_hash_t *entries,
                const char *name,
                apr_uint64_t *modify_flags,
                svn_wc_schedule_t *schedule,
                apr_pool_t *pool)
{
  svn_wc_entry_t *entry, *this_dir_entry;

  /* If we're not supposed to be bothering with this anyway...return. */
  if (! (*modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE))
    return SVN_NO_ERROR;

  /* Get the current entry */
  entry = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

  /* If we're not merging in changes, the requested schedule is the final
     schedule. */
  if (*modify_flags & SVN_WC__ENTRY_MODIFY_FORCE)
    return SVN_NO_ERROR;

  /* The only operation valid on an item not already in revision
     control is addition. */
  if (! entry)
    {
      if (*schedule == svn_wc_schedule_add)
        return SVN_NO_ERROR;
      else
        return
          svn_error_createf(SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
                            _("'%s' is not under version control"),
                            name);
    }

  /* Get the default entry */
  this_dir_entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                                APR_HASH_KEY_STRING);

  /* At this point, we know the following things:

     1. There is already an entry for this item in the entries file
        whose existence is either _normal or _added (or about to
        become such), which for our purposes mean the same thing.

     2. We have been asked to merge in a state change, not to
        explicitly set the state.  */

  /* Here are some cases that are parent-directory sensitive.
     Basically, we make sure that we are not allowing versioned
     resources to just sorta dangle below directories marked for
     deletion. */
  if ((entry != this_dir_entry)
      && (this_dir_entry->schedule == svn_wc_schedule_delete))
    {
      if (*schedule == svn_wc_schedule_add)
        return
          svn_error_createf(SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
                            _("Can't add '%s' to deleted directory; "
                              "try undeleting its parent directory first"),
                            name);
      if (*schedule == svn_wc_schedule_replace)
        return
          svn_error_createf(SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
                            _("Can't replace '%s' in deleted directory; "
                              "try undeleting its parent directory first"),
                            name);
    }

  if (entry->absent && (*schedule == svn_wc_schedule_add))
    {
      return svn_error_createf
        (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
         _("'%s' is marked as absent, so it cannot be scheduled for addition"),
         name);
    }

  switch (entry->schedule)
    {
    case svn_wc_schedule_normal:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
          /* Normal is a trivial no-op case. Reset the
             schedule modification bit and move along. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_delete:
        case svn_wc_schedule_replace:
          /* These are all good. */
          return SVN_NO_ERROR;


        case svn_wc_schedule_add:
          /* You can't add something that's already been added to
             revision control... unless it's got a 'deleted' state */
          if (! entry->deleted)
            return
              svn_error_createf
              (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
               _("Entry '%s' is already under version control"), name);
        }
      break;

    case svn_wc_schedule_add:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
        case svn_wc_schedule_add:
        case svn_wc_schedule_replace:
          /* These are all no-op cases.  Normal is obvious, as is add.
               ### The 'add' case is not obvious: above, we throw an error if
               ### already versioned, so why not here too?
             Replace on an entry marked for addition breaks down to
             (add + (delete + add)), which resolves to just (add), and
             since this entry is already marked with (add), this too
             is a no-op. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_delete:
          /* Not-yet-versioned item being deleted.  If the original
             entry was not marked as "deleted", then remove the entry.
             Else, return the entry to a 'normal' state, preserving
               ### What does it mean for an entry be schedule-add and
               ### deleted at once, and why change schedule to normal?
             the "deleted" flag.  Check that we are not trying to
             remove the SVN_WC_ENTRY_THIS_DIR entry as that would
             leave the entries file in an invalid state. */
          SVN_ERR_ASSERT(entry != this_dir_entry);
          if (! entry->deleted)
            apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);
          else
            *schedule = svn_wc_schedule_normal;
          return SVN_NO_ERROR;
        }
      break;

    case svn_wc_schedule_delete:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
          /* Reverting a delete results in normal */
          return SVN_NO_ERROR;

        case svn_wc_schedule_delete:
          /* These are no-op cases. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_add:
          /* Re-adding an entry marked for deletion?  This is really a
             replace operation. */
          *schedule = svn_wc_schedule_replace;
          return SVN_NO_ERROR;


        case svn_wc_schedule_replace:
          /* Replacing an item marked for deletion breaks down to
             (delete + (delete + add)), which might deserve a warning,
             but whatever. */
          return SVN_NO_ERROR;

        }
      break;

    case svn_wc_schedule_replace:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
          /* Reverting replacements results normal. */
          return SVN_NO_ERROR;

        case svn_wc_schedule_add:
          /* Adding a to-be-replaced entry breaks down to ((delete +
             add) + add) which might deserve a warning, but we'll just
             no-op it. */
        case svn_wc_schedule_replace:
          /* Replacing a to-be-replaced entry breaks down to ((delete
             + add) + (delete + add)), which is insane!  Make up your
             friggin' mind, dude! :-)  Well, we'll no-op this one,
             too. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_delete:
          /* Deleting a to-be-replaced entry breaks down to ((delete +
             add) + delete) which resolves to a flat deletion. */
          *schedule = svn_wc_schedule_delete;
          return SVN_NO_ERROR;

        }
      break;

    default:
      return
        svn_error_createf
        (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
         _("Entry '%s' has illegal schedule"), name);
    }
  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__entry_modify(svn_wc_adm_access_t *adm_access,
                     const char *name,
                     svn_wc_entry_t *entry,
                     apr_uint64_t modify_flags,
                     svn_boolean_t do_sync,
                     apr_pool_t *pool)
{
  apr_hash_t *entries, *entries_nohidden;
  svn_boolean_t entry_was_deleted_p = FALSE;

  SVN_ERR_ASSERT(entry);

  /* Load ADM_ACCESS's whole entries file. */
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, pool));
  SVN_ERR(svn_wc_entries_read(&entries_nohidden, adm_access, FALSE, pool));

  /* Ensure that NAME is valid. */
  if (name == NULL)
    name = SVN_WC_ENTRY_THIS_DIR;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
    {
      svn_wc_entry_t *entry_before, *entry_after;
      apr_uint64_t orig_modify_flags = modify_flags;
      svn_wc_schedule_t orig_schedule = entry->schedule;

      /* Keep a copy of the unmodified entry on hand. */
      entry_before = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

      /* If scheduling changes were made, we have a special routine to
         manage those modifications. */
      SVN_ERR(fold_scheduling(entries, name, &modify_flags,
                              &entry->schedule, pool));

      /* Do a bit of self-testing. The "folding" algorithm should do the
       * same whether we give it the normal entries or all entries including
       * "deleted" ones. Check that it does. */
      /* Note: This pointer-comparison will always be true unless
       * undocumented implementation details are in play, so it's not
       * necessarily saying the contents of the two hashes differ. So this
       * check may be invoked redundantly, but that is harmless. */
      if (entries != entries_nohidden)
        {
          SVN_ERR(fold_scheduling(entries_nohidden, name, &orig_modify_flags,
                                  &orig_schedule, pool));

          /* Make certain that both folding operations had the same
             result. */
          SVN_ERR_ASSERT(orig_modify_flags == modify_flags);
          SVN_ERR_ASSERT(orig_schedule == entry->schedule);
        }

      /* Special case:  fold_state_changes() may have actually REMOVED
         the entry in question!  If so, don't try to fold_entry, as
         this will just recreate the entry again. */
      entry_after = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

      /* Note if this entry was deleted above so we don't accidentally
         re-add it in the following steps. */
      if (entry_before && (! entry_after))
        entry_was_deleted_p = TRUE;
    }

  /* If the entry wasn't just removed from the entries hash, fold the
     changes into the entry. */
  if (! entry_was_deleted_p)
    {
      SVN_ERR(fold_entry(entries, name, modify_flags, entry,
                         svn_wc_adm_access_pool(adm_access)));
      if (entries != entries_nohidden)
        SVN_ERR(fold_entry(entries_nohidden, name, modify_flags, entry,
                           svn_wc_adm_access_pool(adm_access)));
    }

  /* Sync changes to disk. */
  if (do_sync)
    SVN_ERR(svn_wc__entries_write(entries, adm_access, pool));

  return SVN_NO_ERROR;
}


svn_wc_entry_t *
svn_wc_entry_dup(const svn_wc_entry_t *entry, apr_pool_t *pool)
{
  svn_wc_entry_t *dupentry = apr_palloc(pool, sizeof(*dupentry));

  /* Perform a trivial copy ... */
  *dupentry = *entry;

  /* ...and then re-copy stuff that needs to be duped into our pool. */
  if (entry->name)
    dupentry->name = apr_pstrdup(pool, entry->name);
  if (entry->url)
    dupentry->url = apr_pstrdup(pool, entry->url);
  if (entry->repos)
    dupentry->repos = apr_pstrdup(pool, entry->repos);
  if (entry->uuid)
    dupentry->uuid = apr_pstrdup(pool, entry->uuid);
  if (entry->copyfrom_url)
    dupentry->copyfrom_url = apr_pstrdup(pool, entry->copyfrom_url);
  if (entry->conflict_old)
    dupentry->conflict_old = apr_pstrdup(pool, entry->conflict_old);
  if (entry->conflict_new)
    dupentry->conflict_new = apr_pstrdup(pool, entry->conflict_new);
  if (entry->conflict_wrk)
    dupentry->conflict_wrk = apr_pstrdup(pool, entry->conflict_wrk);
  if (entry->prejfile)
    dupentry->prejfile = apr_pstrdup(pool, entry->prejfile);
  if (entry->checksum)
    dupentry->checksum = apr_pstrdup(pool, entry->checksum);
  if (entry->cmt_author)
    dupentry->cmt_author = apr_pstrdup(pool, entry->cmt_author);
  if (entry->lock_token)
    dupentry->lock_token = apr_pstrdup(pool, entry->lock_token);
  if (entry->lock_owner)
    dupentry->lock_owner = apr_pstrdup(pool, entry->lock_owner);
  if (entry->lock_comment)
    dupentry->lock_comment = apr_pstrdup(pool, entry->lock_comment);
  if (entry->changelist)
    dupentry->changelist = apr_pstrdup(pool, entry->changelist);

  /* NOTE: we do not dup cachable_props since it is deprecated. */
  dupentry->cachable_props = SVN_WC__CACHABLE_PROPS;

  if (entry->present_props)
    dupentry->present_props = apr_pstrdup(pool, entry->present_props);
  if (entry->tree_conflict_data)
    dupentry->tree_conflict_data = apr_pstrdup(pool,
                                               entry->tree_conflict_data);
  if (entry->file_external_path)
    dupentry->file_external_path = apr_pstrdup(pool,
                                               entry->file_external_path);
  return dupentry;
}


svn_error_t *
svn_wc__tweak_entry(apr_hash_t *entries,
                    const char *name,
                    const char *new_url,
                    const char *repos,
                    svn_revnum_t new_rev,
                    svn_boolean_t allow_removal,
                    svn_boolean_t *write_required,
                    apr_pool_t *pool)
{
  svn_wc_entry_t *entry;

  entry = apr_hash_get(entries, name, APR_HASH_KEY_STRING);
  if (! entry)
    return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                             _("No such entry: '%s'"), name);

  if (new_url != NULL
      && (! entry->url || strcmp(new_url, entry->url)))
    {
      *write_required = TRUE;
      entry->url = apr_pstrdup(pool, new_url);
    }

  if (repos != NULL
      && (! entry->repos || strcmp(repos, entry->repos))
      && entry->url
      && svn_path_is_ancestor(repos, entry->url))
    {
      svn_boolean_t set_repos = TRUE;

      /* Setting the repository root on THIS_DIR will make files in this
         directory inherit that property.  So to not make the WC corrupt,
         we have to make sure that the repos root is valid for such entries as
         well.  Note that this shouldn't happen in normal circumstances. */
      if (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)
        {
          apr_hash_index_t *hi;
          for (hi = apr_hash_first(pool, entries); hi;
               hi = apr_hash_next(hi))
            {
              void *value;
              const svn_wc_entry_t *child_entry;

              apr_hash_this(hi, NULL, NULL, &value);
              child_entry = value;

              if (! child_entry->repos && child_entry->url
                  && ! svn_path_is_ancestor(repos, child_entry->url))
                {
                  set_repos = FALSE;
                  break;
                }
            }
        }

      if (set_repos)
        {
          *write_required = TRUE;
          entry->repos = apr_pstrdup(pool, repos);
        }
    }

  if ((SVN_IS_VALID_REVNUM(new_rev))
      && (entry->schedule != svn_wc_schedule_add)
      && (entry->schedule != svn_wc_schedule_replace)
      && (entry->copied != TRUE)
      && (entry->revision != new_rev))
    {
      *write_required = TRUE;
      entry->revision = new_rev;
    }

  /* As long as this function is only called as a helper to
     svn_wc__do_update_cleanup, then it's okay to remove any entry
     under certain circumstances:

     If the entry is still marked 'deleted', then the server did not
     re-add it.  So it's really gone in this revision, thus we remove
     the entry.

     If the entry is still marked 'absent' and yet is not the same
     revision as new_rev, then the server did not re-add it, nor
     re-absent it, so we can remove the entry.

     ### This function cannot always determine whether removal is
     ### appropriate, hence the ALLOW_REMOVAL flag.  It's all a bit of a
     ### mess. */
  if (allow_removal
      && (entry->deleted || (entry->absent && entry->revision != new_rev)))
    {
      *write_required = TRUE;
      apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);
    }

  return SVN_NO_ERROR;
}




/*** Initialization of the entries file. ***/

svn_error_t *
svn_wc__entries_init(const char *path,
                     const char *uuid,
                     const char *url,
                     const char *repos,
                     svn_revnum_t initial_rev,
                     svn_depth_t depth,
                     apr_pool_t *pool)
{
  svn_stream_t *stream;
  const char *temp_file_path;
  svn_stringbuf_t *accum = svn_stringbuf_createf(pool, "%d\n",
                                                 SVN_WC__VERSION);
  svn_wc_entry_t *entry = alloc_entry(pool);
  apr_size_t len;

  SVN_ERR_ASSERT(! repos || svn_path_is_ancestor(repos, url));
  SVN_ERR_ASSERT(depth == svn_depth_empty
                 || depth == svn_depth_files
                 || depth == svn_depth_immediates
                 || depth == svn_depth_infinity);

  /* Create the entries file, which must not exist prior to this. */
  SVN_ERR(svn_wc__open_adm_writable(&stream, &temp_file_path,
                                    path, SVN_WC__ADM_ENTRIES, pool, pool));

  /* Add an entry for the dir itself.  The directory has no name.  It
     might have a UUID, but otherwise only the revision and default
     ancestry are present as XML attributes, and possibly an
     'incomplete' flag if the revnum is > 0. */

  entry->kind = svn_node_dir;
  entry->url = url;
  entry->revision = initial_rev;
  entry->uuid = uuid;
  entry->repos = repos;
  entry->depth = depth;
  if (initial_rev > 0)
    entry->incomplete = TRUE;

  SVN_ERR(write_entry(accum, entry, SVN_WC_ENTRY_THIS_DIR, entry, pool));

  len = accum->len;
  SVN_ERR_W(svn_stream_write(stream, accum->data, &len),
            apr_psprintf(pool,
                         _("Error writing entries file for '%s'"),
                         svn_path_local_style(path, pool)));

  /* Now we have a `entries' file with exactly one entry, an entry
     for this dir.  Close the file and sync it up. */
  return svn_wc__close_adm_stream(stream, temp_file_path, path,
                                  SVN_WC__ADM_ENTRIES, pool);
}


/*--------------------------------------------------------------- */

/*** Generic Entry Walker */


/* A recursive entry-walker, helper for svn_wc_walk_entries3().
 *
 * For this directory (DIRPATH, ADM_ACCESS), call the "found_entry" callback
 * in WALK_CALLBACKS, passing WALK_BATON to it. Then, for each versioned
 * entry in this directory, call the "found entry" callback and then recurse
 * (if it is a directory and if DEPTH allows).
 *
 * If SHOW_HIDDEN is true, include entries that are in a 'deleted' or
 * 'absent' state (and not scheduled for re-addition), else skip them.
 *
 * Call CANCEL_FUNC with CANCEL_BATON to allow cancellation.
 */
static svn_error_t *
walker_helper(const char *dirpath,
              svn_wc_adm_access_t *adm_access,
              const svn_wc_entry_callbacks2_t *walk_callbacks,
              void *walk_baton,
              svn_depth_t depth,
              svn_boolean_t show_hidden,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_wc_entry_t *dot_entry;

  SVN_ERR(walk_callbacks->handle_error
          (dirpath, svn_wc_entries_read(&entries, adm_access, show_hidden,
                                        pool), walk_baton, pool));

  /* As promised, always return the '.' entry first. */
  dot_entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                           APR_HASH_KEY_STRING);
  if (! dot_entry)
    return walk_callbacks->handle_error
      (dirpath, svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                                  _("Directory '%s' has no THIS_DIR entry"),
                                  svn_path_local_style(dirpath, pool)),
       walk_baton, pool);

  /* Call the "found entry" callback for this directory as a "this dir"
   * entry. Note that if this directory has been reached by recusrion, this
   * is the second visit as it will already have been visited once as a
   * child entry of its parent. */
  SVN_ERR(walk_callbacks->handle_error
          (dirpath,
           walk_callbacks->found_entry(dirpath, dot_entry, walk_baton, pool),
           walk_baton, pool));

  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  /* Loop over each of the other entries. */
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const svn_wc_entry_t *current_entry;
      const char *entrypath;

      svn_pool_clear(subpool);

      /* See if someone wants to cancel this operation. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      apr_hash_this(hi, &key, NULL, &val);
      current_entry = val;

      /* Skip the "this dir" entry. */
      if (strcmp(current_entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      entrypath = svn_path_join(dirpath, key, subpool);

      /* Call the "found entry" callback for this entry. (For a directory,
       * this is the first visit: as a child.) */
      if (current_entry->kind == svn_node_file
          || depth >= svn_depth_immediates)
        {
          SVN_ERR(walk_callbacks->handle_error
                  (entrypath,
                   walk_callbacks->found_entry(entrypath, current_entry,
                                               walk_baton, subpool),
                   walk_baton, pool));
        }

      /* Recurse into this entry if appropriate. */
      if (current_entry->kind == svn_node_dir
          && !entry_is_hidden(current_entry)
          && depth >= svn_depth_immediates)
        {
          svn_wc_adm_access_t *entry_access;
          svn_depth_t depth_below_here = depth;

          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(walk_callbacks->handle_error
                  (entrypath,
                   svn_wc_adm_retrieve(&entry_access, adm_access, entrypath,
                                       subpool),
                   walk_baton, pool));

          if (entry_access)
            SVN_ERR(walker_helper(entrypath, entry_access,
                                  walk_callbacks, walk_baton,
                                  depth_below_here, show_hidden,
                                  cancel_func, cancel_baton,
                                  subpool));
        }
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__walker_default_error_handler(const char *path,
                                     svn_error_t *err,
                                     void *walk_baton,
                                     apr_pool_t *pool)
{
  return err;
}


/* The public API. */
svn_error_t *
svn_wc_walk_entries3(const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const svn_wc_entry_callbacks2_t *walk_callbacks,
                     void *walk_baton,
                     svn_depth_t depth,
                     svn_boolean_t show_hidden,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, show_hidden, pool));

  if (! entry)
    return walk_callbacks->handle_error
      (path, svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                               _("'%s' is not under version control"),
                               svn_path_local_style(path, pool)),
       walk_baton, pool);

  if (entry->kind == svn_node_file || entry->depth == svn_depth_exclude)
    return walk_callbacks->handle_error
      (path, walk_callbacks->found_entry(path, entry, walk_baton, pool),
       walk_baton, pool);

  else if (entry->kind == svn_node_dir)
    return walker_helper(path, adm_access, walk_callbacks, walk_baton,
                         depth, show_hidden, cancel_func, cancel_baton, pool);

  else
    return walk_callbacks->handle_error
      (path, svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                               _("'%s' has an unrecognized node kind"),
                               svn_path_local_style(path, pool)),
       walk_baton, pool);
}


/* A baton for use with visit_tc_too_callbacks. */
typedef struct visit_tc_too_baton_t
  {
    svn_wc_adm_access_t *adm_access;
    const svn_wc_entry_callbacks2_t *callbacks;
    void *baton;
    const char *target;
    svn_depth_t depth;
  } visit_tc_too_baton_t;

/* An svn_wc_entry_callbacks2_t callback function.
 *
 * Call the user's "found entry" callback
 * WALK_BATON->callbacks->found_entry(), passing it PATH, ENTRY and
 * WALK_BATON->baton. Then call it once for each unversioned tree-conflicted
 * child of this entry, passing it the child path, a null "entry", and
 * WALK_BATON->baton. WALK_BATON is of type (visit_tc_too_baton_t *).
 */
static svn_error_t *
visit_tc_too_found_entry(const char *path,
                         const svn_wc_entry_t *entry,
                         void *walk_baton,
                         apr_pool_t *pool)
{
  struct visit_tc_too_baton_t *baton = walk_baton;
  svn_boolean_t check_children;

  /* Call the entry callback for this entry. */
  SVN_ERR(baton->callbacks->found_entry(path, entry, baton->baton, pool));

  if (entry->kind != svn_node_dir || entry_is_hidden(entry))
    return SVN_NO_ERROR;

  /* If this is a directory, we may need to also visit any unversioned
   * children that are tree conflict victims. However, that should not
   * happen when we've already reached the requested depth. */

  switch (baton->depth){
    case svn_depth_empty:
      check_children = FALSE;
      break;

    /* Since svn_depth_files only visits files and this is a directory,
     * we have to be at the target. Just verify that anyway: */
    case svn_depth_files:
    case svn_depth_immediates:
      /* Check if this already *is* an immediate child, in which
       * case we shouldn't descend further. */
      check_children = (strcmp(baton->target, path) == 0);
      break;

    case svn_depth_infinity:
    case svn_depth_exclude:
    case svn_depth_unknown:
      check_children = TRUE;
      break;
  };

  if (check_children)
    {
      /* We're supposed to check the children of this directory. However,
       * in case of svn_depth_files, don't visit directories. */

      svn_wc_adm_access_t *adm_access = NULL;
      apr_array_header_t *conflicts;
      int i;

      /* Loop through all the tree conflict victims */
      SVN_ERR(svn_wc__read_tree_conflicts(&conflicts,
                                          entry->tree_conflict_data, path,
                                          pool));

      if (conflicts->nelts > 0)
        SVN_ERR(svn_wc_adm_retrieve(&adm_access, baton->adm_access, path,
                                    pool));

      for (i = 0; i < conflicts->nelts; i++)
        {
          svn_wc_conflict_description_t *conflict
            = APR_ARRAY_IDX(conflicts, i, svn_wc_conflict_description_t *);
          const svn_wc_entry_t *child_entry;

          if ((conflict->node_kind == svn_node_dir)
              && (baton->depth == svn_depth_files))
            continue;

          /* If this victim is not in this dir's entries ... */
          SVN_ERR(svn_wc_entry(&child_entry, conflict->path, adm_access,
                               TRUE, pool));
          if (!child_entry || child_entry->deleted)
            {
              /* Found an unversioned tree conflict victim. Call the "found
               * entry" callback with a null "entry" parameter. */
              SVN_ERR(baton->callbacks->found_entry(conflict->path, NULL,
                                                    baton->baton, pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

/* An svn_wc_entry_callbacks2_t callback function.
 *
 * If the error ERR is because this PATH is an unversioned tree conflict
 * victim, call the user's "found entry" callback
 * WALK_BATON->callbacks->found_entry(), passing it this PATH, a null
 * "entry" parameter, and WALK_BATON->baton. Otherwise, forward this call
 * to the user's "handle error" callback
 * WALK_BATON->callbacks->handle_error().
 */
static svn_error_t *
visit_tc_too_error_handler(const char *path,
                           svn_error_t *err,
                           void *walk_baton,
                           apr_pool_t *pool)
{
  struct visit_tc_too_baton_t *baton = walk_baton;

  /* If this is an unversioned tree conflict victim, call the "found entry"
   * callback. This can occur on the root node of the walk; we do not expect
   * to reach such a node by recursion. */
  if (err && (err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE))
    {
      svn_wc_adm_access_t *adm_access;
      svn_wc_conflict_description_t *conflict;
      char *parent_path = svn_path_dirname(path, pool);

      /* See if there is any tree conflict on this path. */
      SVN_ERR(svn_wc_adm_retrieve(&adm_access, baton->adm_access, parent_path,
                                  pool));
      SVN_ERR(svn_wc__get_tree_conflict(&conflict, path, adm_access, pool));

      /* If so, don't regard it as an error but call the "found entry"
       * callback with a null "entry" parameter. */
      if (conflict)
        {
          svn_error_clear(err);
          err = NULL;

          SVN_ERR(baton->callbacks->found_entry(conflict->path, NULL,
                                                baton->baton, pool));
        }
    }

  /* Call the user's error handler for this entry. */
  return baton->callbacks->handle_error(path, err, baton->baton, pool);
}

/* Callbacks used by svn_wc_walk_entries_and_tc(). */
static const svn_wc_entry_callbacks2_t
visit_tc_too_callbacks =
  {
    visit_tc_too_found_entry,
    visit_tc_too_error_handler
  };

svn_error_t *
svn_wc__walk_entries_and_tc(const char *path,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_entry_callbacks2_t *walk_callbacks,
                            void *walk_baton,
                            svn_depth_t depth,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_adm_access_t *path_adm_access;
  const svn_wc_entry_t *entry;

  /* If there is no adm_access, there are no nodes to visit, not even 'path'
   * because it can't be in conflict. */
  if (adm_access == NULL)
    return SVN_NO_ERROR;

  /* Is 'path' versioned? Set path_adm_access accordingly. */
  /* First: Get item's adm access (meaning parent's if it's a file). */
  err = svn_wc_adm_probe_retrieve(&path_adm_access, adm_access, path, pool);
  if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* Item is unversioned and doesn't have a versioned parent so there is
       * nothing to walk. */
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;
  /* If we can get the item's entry then it is versioned. */
  err = svn_wc_entry(&entry, path, path_adm_access, TRUE, pool);
  if (err)
    {
      svn_error_clear(err);
      /* Indicate that it is unversioned. */
      entry = NULL;
    }

  /* If this path is versioned, do a tree walk, else perhaps call the
   * "unversioned tree conflict victim" callback directly. */
  if (entry)
    {
      /* Versioned, so use the regular entries walker with callbacks that
       * make it also visit unversioned tree conflict victims. */
      visit_tc_too_baton_t visit_tc_too_baton;

      visit_tc_too_baton.adm_access = adm_access;
      visit_tc_too_baton.callbacks = walk_callbacks;
      visit_tc_too_baton.baton = walk_baton;
      visit_tc_too_baton.target = path;
      visit_tc_too_baton.depth = depth;

      SVN_ERR(svn_wc_walk_entries3(path, path_adm_access,
                                   &visit_tc_too_callbacks, &visit_tc_too_baton,
                                   depth, TRUE /*show_hidden*/,
                                   cancel_func, cancel_baton, pool));
    }
  else
    {
      /* Not locked, so assume unversioned. If it is a tree conflict victim,
       * call the "found entry" callback with a null "entry" parameter. */
      svn_wc_conflict_description_t *conflict;

      SVN_ERR(svn_wc__get_tree_conflict(&conflict, path, adm_access, pool));
      if (conflict)
        SVN_ERR(walk_callbacks->found_entry(path, NULL, walk_baton, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_mark_missing_deleted(const char *path,
                            svn_wc_adm_access_t *parent,
                            apr_pool_t *pool)
{
  svn_node_kind_t pkind;

  SVN_ERR(svn_io_check_path(path, &pkind, pool));

  if (pkind == svn_node_none)
    {
      const char *parent_path, *bname;
      svn_wc_adm_access_t *adm_access;
      svn_wc_entry_t newent;

      newent.deleted = TRUE;
      newent.schedule = svn_wc_schedule_normal;

      svn_path_split(path, &parent_path, &bname, pool);

      SVN_ERR(svn_wc_adm_retrieve(&adm_access, parent, parent_path, pool));
      return svn_wc__entry_modify(adm_access, bname, &newent,
                                   (SVN_WC__ENTRY_MODIFY_DELETED
                                    | SVN_WC__ENTRY_MODIFY_SCHEDULE
                                    | SVN_WC__ENTRY_MODIFY_FORCE),
                                   TRUE, /* sync right away */ pool);
    }
  else
    return svn_error_createf(SVN_ERR_WC_PATH_FOUND, NULL,
                             _("Unexpectedly found '%s': "
                               "path is marked 'missing'"),
                             svn_path_local_style(path, pool));
}
