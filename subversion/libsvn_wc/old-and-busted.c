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


/* Used when reading an entries file in XML format. */
struct entries_accumulator
{
  /* Keys are entry names, vals are (struct svn_wc_entry_t *)'s. */
  apr_hash_t *entries;

  /* The parser that's parsing it, for signal_expat_bailout(). */
  svn_xml_parser_t *parser;

  /* Should we include 'deleted' entries in the hash? */
  svn_boolean_t show_hidden;

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
  if (!entry_is_hidden(entry) || accum->show_hidden)
    apr_hash_set(accum->entries, entry->name, APR_HASH_KEY_STRING, entry);
}

/* Parse BUF of size SIZE as an entries file in XML format, storing the parsed
   entries in ENTRIES.  Use pool for temporary allocations and the pool of
   ADM_ACCESS for the returned entries. */
static svn_error_t *
parse_entries_xml(svn_wc_adm_access_t *adm_access,
                  apr_hash_t *entries,
                  svn_boolean_t show_hidden,
                  const char *buf,
                  apr_size_t size,
                  apr_pool_t *pool)
{
  svn_xml_parser_t *svn_parser;
  struct entries_accumulator accum;

  /* Set up userData for the XML parser. */
  accum.entries = entries;
  accum.show_hidden = show_hidden;
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
  SVN_ERR(read_val(&entry->cachable_props, buf, end));
  if (entry->cachable_props)
    entry->cachable_props = apr_pstrdup(pool, entry->cachable_props);
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

  if (! dst->cachable_props)
    dst->cachable_props = src->cachable_props;
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

/* Fill the entries cache in ADM_ACCESS. Either the full hash cache will be
   populated, if SHOW_HIDDEN is TRUE, or the truncated hash cache will be
   populated if SHOW_HIDDEN is FALSE.  POOL is used for local memory
   allocation, the access baton pool is used for the cache. */
static svn_error_t *
read_entries_old(svn_wc_adm_access_t *adm_access,
                 svn_boolean_t show_hidden,
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
    SVN_ERR(parse_entries_xml(adm_access, entries, show_hidden,
                              buf->data, buf->len, scratch_pool));
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

          if ((entry->depth != svn_depth_exclude)
              || (!entry_is_hidden(entry) || show_hidden))
            {
              apr_hash_set(entries, entry->name, APR_HASH_KEY_STRING, entry);
            }
        }
    }

  /* Fill in any implied fields. */
  SVN_ERR(resolve_to_defaults(entries, result_pool));

  svn_wc__adm_access_set_entries(adm_access, show_hidden, entries);

  return SVN_NO_ERROR;
}
