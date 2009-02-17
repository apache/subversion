/*
 * old-and-busted.c:  routines for reading pre-1.7 working copies.
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



#include "svn_time.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_ctype.h"
#include "svn_pools.h"

#include "wc.h"
#include "lock.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"

#include "private/svn_wc_private.h"
#include "svn_private_config.h"

/* Some old defines which this file might need. */
#define SVN_WC__ENTRY_ATTR_HAS_PROPS          "has-props"
#define SVN_WC__ENTRY_ATTR_HAS_PROP_MODS      "has-prop-mods"
#define SVN_WC__ENTRY_ATTR_CACHABLE_PROPS     "cachable-props"
#define SVN_WC__ENTRY_ATTR_PRESENT_PROPS      "present-props"
#define SVN_WC__ENTRY_MODIFY_HAS_PROPS          APR_INT64_C(0x0000000004000000)
#define SVN_WC__ENTRY_MODIFY_HAS_PROP_MODS      APR_INT64_C(0x0000000008000000)
#define SVN_WC__ENTRY_MODIFY_CACHABLE_PROPS     APR_INT64_C(0x0000000010000000)
#define SVN_WC__ENTRY_MODIFY_PRESENT_PROPS      APR_INT64_C(0x0000000020000000)





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

  /* has-props, has-prop-mods, cachable-props, present-props are all
     deprecated. Read any values that may be in the 'entries' file, but
     discard them, and just put default values into the entry. */
  {
    const char *unused_value;

    /* has-props flag. */
    SVN_ERR(read_val(&unused_value, buf, end));
    entry->has_props = FALSE;
    MAYBE_DONE;

    /* has-prop-mods flag. */
    SVN_ERR(read_val(&unused_value, buf, end));
    entry->has_prop_mods = FALSE;
    MAYBE_DONE;

    /* Use the empty string for cachable_props, indicating that we no
       longer attempt to cache any properties. An empty string for
       present_props means that no cachable props are present. */

    /* cachable-props string. */
    SVN_ERR(read_val(&unused_value, buf, end));
    entry->cachable_props = "";
    MAYBE_DONE;

    /* present-props string. */
    SVN_ERR(read_val(&unused_value, buf, end));
    entry->present_props = "";
    MAYBE_DONE;
  }

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
    dst->url = svn_path_url_add_component(src->url, dst->name, pool);

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
svn_error_t *
svn_wc__read_entries_old(svn_wc_adm_access_t *adm_access,
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




/* *******************
 *
 * Below is code to write the old-format 'entries' file. This code will
 * eventually disappear, as the eventual plan is to NEVER write the old
 * format. A working copy must be upgraded before use, so (eventually)
 * this functionality will not be required.
 *
 * ******************* */

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

  /* has-props, has-prop-mods, cachable-props, present-props are all
     deprecated, so write nothing for them. */
  write_val(buf, NULL, 0);
  write_val(buf, NULL, 0);
  write_val(buf, NULL, 0);
  write_val(buf, NULL, 0);

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

  /* has_props, has_prop_mods, cachable_props, and present_props are all
     deprecated, so do not add any attributes. */

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
svn_wc__entries_write_old(apr_hash_t *entries,
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
