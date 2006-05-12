/*
 * entries.c :  manipulating the administrative `entries' file.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include <assert.h>

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

#include "svn_private_config.h"


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
  return entry;
}


/* If attribute ATTR_NAME appears in hash ATTS, set *ENTRY_FLAG to its
 * boolean value and add MODIFY_FLAG into *MODIFY_FLAGS, else set *ENTRY_FLAG
 * false.  ENTRY_NAME is the name of the WC-entry. */
static svn_error_t *
do_bool_attr(svn_boolean_t *entry_flag,
             apr_uint32_t *modify_flags, apr_uint32_t modify_flag,
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
  *result = val;
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
   copyed and the buffer is modified in place, by replacing the
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

/* Allocate an entry from POOL and read it from [*BUF, END).  The
   buffer may be modified in place while parsing.  Return the new
   entry in *NEW_ENTRY.  Advance *BUF to point at the end of the entry
   record. */
static svn_error_t *
read_entry(svn_wc_entry_t **new_entry,
           char **buf, const char *end,
           apr_pool_t *pool)
{
  svn_wc_entry_t *entry = alloc_entry(pool);
  const char *name;
  
#define MAYBE_DONE if (**buf == '\f') goto done

  /* Find the name and set up the entry under that name. */
  SVN_ERR(read_str(&name, buf, end, pool));
  entry->name = name ? name : SVN_WC_ENTRY_THIS_DIR;

  /* Set up kind. */
  {
    const char *kindstr;
    SVN_ERR(read_val(&kindstr, buf, end));
    if (kindstr)
      {
        if (! strcmp(kindstr, SVN_WC__ENTRIES_ATTR_FILE_STR))
          entry->kind = svn_node_file;
        else if (! strcmp(kindstr, SVN_WC__ENTRIES_ATTR_DIR_STR))
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
  SVN_ERR(read_str(&entry->url, buf, end, pool));
  MAYBE_DONE;

  /* Set up repository root.  Make sure it is a prefix of url. */
  SVN_ERR(read_str(&entry->repos, buf, end, pool));
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
        if (! strcmp(schedulestr, SVN_WC__ENTRY_VALUE_ADD))
          entry->schedule = svn_wc_schedule_add;
        else if (! strcmp(schedulestr, SVN_WC__ENTRY_VALUE_DELETE))
          entry->schedule = svn_wc_schedule_delete;
        else if (! strcmp(schedulestr, SVN_WC__ENTRY_VALUE_REPLACE))
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
    SVN_ERR(read_str(&entry->prejfile, buf, end, pool));
    MAYBE_DONE;
    SVN_ERR(read_str(&entry->conflict_old, buf, end, pool));
    MAYBE_DONE;
    SVN_ERR(read_str(&entry->conflict_new, buf, end, pool));
    MAYBE_DONE;
    SVN_ERR(read_str(&entry->conflict_wrk, buf, end, pool));
    MAYBE_DONE;
  }

  /* Is this entry copied? */
  SVN_ERR(read_bool(&entry->copied, SVN_WC__ENTRY_ATTR_COPIED, buf, end));
  MAYBE_DONE;

  SVN_ERR(read_str(&entry->copyfrom_url, buf, end, pool));
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
  
 done:;
  *new_entry = entry;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__atts_to_entry(svn_wc_entry_t **new_entry,
                      apr_uint32_t *modify_flags,
                      apr_hash_t *atts,
                      apr_pool_t *pool)
{
  svn_wc_entry_t *entry = alloc_entry(pool);
  const char *name;
  
  *modify_flags = 0;

  /* Find the name and set up the entry under that name. */
  name = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_NAME, APR_HASH_KEY_STRING);
  /* XXX Replace the obsolete "svn:this_dir".
     XXX This code should go away by 1.0 */
  {
    if (name && strcmp(name, "svn:this_dir") == 0)
      name = SVN_WC_ENTRY_THIS_DIR;
  }
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
        if (! strcmp(kindstr, SVN_WC__ENTRIES_ATTR_FILE_STR))
          entry->kind = svn_node_file;
        else if (! strcmp(kindstr, SVN_WC__ENTRIES_ATTR_DIR_STR))
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
        if (! strcmp(schedulestr, SVN_WC__ENTRY_VALUE_ADD))
          entry->schedule = svn_wc_schedule_add;
        else if (! strcmp(schedulestr, SVN_WC__ENTRY_VALUE_DELETE))
              entry->schedule = svn_wc_schedule_delete;
        else if (! strcmp(schedulestr, SVN_WC__ENTRY_VALUE_REPLACE))
          entry->schedule = svn_wc_schedule_replace;
        else if (! strcmp(schedulestr, ""))
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


  /* Attempt to set up timestamps. */
  {
    const char *text_timestr, *prop_timestr;
    
    text_timestr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_TEXT_TIME,
                                APR_HASH_KEY_STRING);
    if (text_timestr)
      {
        if (! strcmp(text_timestr, SVN_WC__TIMESTAMP_WC))
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
    
    prop_timestr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_PROP_TIME,
                                APR_HASH_KEY_STRING);
    if (prop_timestr)
      {
        if (! strcmp(prop_timestr, SVN_WC__TIMESTAMP_WC))
          {
            /* Special case:  a magic string that means 'get this value
               from the working copy' -- we ignore it here, trusting
               that the caller of this function know what to do about
               it.  */
          }
        else
          SVN_ERR(svn_time_from_cstring(&entry->prop_time, prop_timestr,
                                        pool));
        
        *modify_flags |= SVN_WC__ENTRY_MODIFY_PROP_TIME;
      }
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
  
  /* cachable-props string. */
  entry->cachable_props = apr_hash_get(atts, 
                                       SVN_WC__ENTRY_ATTR_CACHABLE_PROPS,
                                       APR_HASH_KEY_STRING);
  if (entry->cachable_props)
    {
      *modify_flags |= SVN_WC__ENTRY_MODIFY_CACHABLE_PROPS;
      entry->cachable_props = apr_pstrdup(pool, entry->cachable_props);
    }

  /* present-props string. */
  entry->present_props = apr_hash_get(atts, 
                                      SVN_WC__ENTRY_ATTR_PRESENT_PROPS,
                                      APR_HASH_KEY_STRING);
  if (entry->present_props)
    {
      *modify_flags |= SVN_WC__ENTRY_MODIFY_PRESENT_PROPS;
      entry->present_props = apr_pstrdup(pool, entry->present_props);
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

  /* Should we include 'deleted' entries in the hash? */
  svn_boolean_t show_hidden;

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
  apr_uint32_t modify_flags = 0;

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
     have made it into SVN_WC_ENTRY_THIS_DIR.  (Note that technically,
     an entry can't be both absent and scheduled for addition, but we
     don't need a sanity check for that here.) */
  if ((entry->deleted || entry->absent)
      && (entry->schedule != svn_wc_schedule_add)
      && (entry->schedule != svn_wc_schedule_replace)
      && (! accum->show_hidden))
    ;
  else
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
read_entries(svn_wc_adm_access_t *adm_access,
             svn_boolean_t show_hidden,
             apr_pool_t *pool)
{
  const char *path = svn_wc_adm_access_path(adm_access);
  apr_file_t *infile = NULL;
  svn_stringbuf_t *buf = svn_stringbuf_create("", pool);
  apr_hash_t *entries = apr_hash_make(svn_wc_adm_access_pool(adm_access));
  char *curp, *endp;
  svn_wc_entry_t *entry;
  int entryno;

  /* Open the entries file. */
  SVN_ERR(svn_wc__open_adm_file(&infile, path,
                                SVN_WC__ADM_ENTRIES, APR_READ, pool));

  SVN_ERR(svn_stringbuf_from_aprfile(&buf, infile, pool));

  curp = buf->data;
  endp = buf->data + buf->len;

  /* If the first byte of the file is not a digit, then it is probably in XML
     format. */
  if (curp != endp && !svn_ctype_isdigit(*curp))
    SVN_ERR(parse_entries_xml(adm_access, entries, show_hidden,
                              buf->data, buf->len, pool));
  else
    {
      /* Skip format line. */
      /* ### Could read it here and report it to caller if it wants it. */
      curp = memchr(curp, '\n', buf->len);
      if (! curp)
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                 _("Invalid version line in entries file "
                                   "of '%s'"),
                                 svn_path_local_style(path, pool));
      ++curp;
      entryno = 1;

      while (curp != endp)
        {
          svn_error_t *err = read_entry(&entry, &curp, endp,
                                        svn_wc_adm_access_pool(adm_access));
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
                                     entryno, svn_path_local_style(path, pool));
      
          ++curp;
          ++entryno;
          if ((entry->deleted || entry->absent)
              && (entry->schedule != svn_wc_schedule_add)
              && (entry->schedule != svn_wc_schedule_replace)
              && (! show_hidden))
            ;
          else
            apr_hash_set(entries, entry->name, APR_HASH_KEY_STRING, entry);
        }
    }

  /* Close the entries file. */
  SVN_ERR(svn_wc__close_adm_file(infile, svn_wc_adm_access_path(adm_access),
                                 SVN_WC__ADM_ENTRIES, 0, pool));

  /* Fill in any implied fields. */
  SVN_ERR(resolve_to_defaults(entries, svn_wc_adm_access_pool(adm_access)));

  svn_wc__adm_access_set_entries(adm_access, show_hidden, entries);

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


#if 0
/* This is #if 0'd out until I decide where to use it. --cmpilato */

/* Run a simple validity check on the ENTRIES (the list of entries
   associated with the directory PATH). */
static svn_error_t *
check_entries(apr_hash_t *entries,
              const char *path,
              apr_pool_t *pool)
{
  svn_wc_entry_t *default_entry;
  apr_hash_index_t *hi;

  default_entry = apr_hash_get(entries, 
                               SVN_WC_ENTRY_THIS_DIR, 
                               APR_HASH_KEY_STRING);
  if (! default_entry)
    return svn_error_createf
      (SVN_ERR_WC_CORRUPT, NULL,
       _("Corrupt working copy: '%s' has no default entry"),
       svn_path_local_style(path, pool));

  /* Validate DEFAULT_ENTRY's current schedule. */
  switch (default_entry->schedule)
    {
    case svn_wc_schedule_normal:
    case svn_wc_schedule_add:
    case svn_wc_schedule_delete:
    case svn_wc_schedule_replace:
      /* These are all valid states */
      break;

    default:
      /* This is an invalid state */
      return svn_error_createf
        (SVN_ERR_WC_CORRUPT, NULL,
         _("Corrupt working copy: directory '%s' has an invalid schedule"),
         svn_path_local_style(path, pool));
    }
  
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *name;
      void *val;
      svn_wc_entry_t *this_entry;

      /* Get the entry */
      apr_hash_this(hi, &key, NULL, &val);
      this_entry = val;
      name = key;

      /* We've already checked the "this dir" entry */
      if (! strcmp(name, SVN_WC_ENTRY_THIS_DIR ))
        continue;

      /* Validate THIS_ENTRY's current schedule. */
      switch (this_entry->schedule)
        {
        case svn_wc_schedule_normal:
        case svn_wc_schedule_add:
        case svn_wc_schedule_delete:
        case svn_wc_schedule_replace:
          /* These are all valid states */
          break;

        default:
          /* This is an invalid state */
          return svn_error_createf
            (SVN_ERR_WC_CORRUPT, NULL,
             _("Corrupt working copy: "
               "'%s' in directory '%s' has an invalid schedule"),
             name, svn_path_local_style(path, pool));
        }

      if ((default_entry->schedule == svn_wc_schedule_add)
          && (this_entry->schedule != svn_wc_schedule_add))
        return svn_error_createf
          (SVN_ERR_WC_CORRUPT, NULL,
           _("Corrupt working copy: '%s' in directory '%s' (which is "
             "scheduled for addition) is not itself scheduled for addition"),
           name, svn_path_local_style(path, pool));
  
      if ((default_entry->schedule == svn_wc_schedule_delete)
          && (this_entry->schedule != svn_wc_schedule_delete))
        return svn_error_createf
          (SVN_ERR_WC_CORRUPT, NULL,
           _("Corrupt working copy: '%s' in directory '%s' (which is "
             "scheduled for deletion) is not itself scheduled for deletion"),
           name, svn_path_local_style(path, pool));

      if ((default_entry->schedule == svn_wc_schedule_replace)
          && (this_entry->schedule == svn_wc_schedule_normal))
        return svn_error_createf
          (SVN_ERR_WC_CORRUPT, NULL,
           _("Corrupt working copy: '%s' in directory '%s' (which is "
             "scheduled for replacement) has an invalid schedule"),
           name, svn_path_local_style(path, pool));
    }
  
  return SVN_NO_ERROR;
}
#endif /* 0 */


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
      SVN_ERR(read_entries(adm_access, TRUE, pool));

      new_entries = svn_wc__adm_access_entries(adm_access, show_hidden, pool);
    }

  *entries = new_entries;
  return SVN_NO_ERROR;
}

/* Append STR to BUF, terminating it with a newline.  Escape bytes that
   needs escaping.  Use POOL for temporary allocations. */
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
   bytes. */
static void
write_val(svn_stringbuf_t *buf, const char *val, apr_size_t len)
{
  if (val)
    svn_stringbuf_appendbytes(buf, val, len);
  svn_stringbuf_appendbytes(buf, "\n", 1);
}

/* If VAL is true, append FIELD_NAME followede by a terminator to BUF.
   Else, just append the terminator. */
static void
write_bool(svn_stringbuf_t *buf, const char *field_name, svn_boolean_t val)
{
  write_val(buf, val ? field_name : NULL, val ? strlen(field_name) : 0);
}

/* Append the representation of REVNUM to BUF and a terminator, using
   POOL for temporary allocations. */
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
static void
write_entry(svn_stringbuf_t *buf,
            svn_wc_entry_t *entry,
            const char *name,
            svn_wc_entry_t *this_dir,
            apr_pool_t *pool)
{
  const char *valuestr;
  svn_revnum_t valuerev;
  svn_boolean_t is_this_dir = strcmp(name, SVN_WC_ENTRY_THIS_DIR) == 0;
  svn_boolean_t is_subdir = ! is_this_dir && (entry->kind == svn_node_dir);

  assert(name);

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
      (! is_subdir && strcmp(svn_path_url_add_component(this_dir->url, name,
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

  /* cachable-props string. */
  if (is_this_dir
      || ! this_dir->cachable_props || ! entry->cachable_props
      || strcmp(this_dir->cachable_props, entry->cachable_props) != 0)
    valuestr = entry->cachable_props;
  else
    valuestr = NULL;
  write_val(buf, valuestr, valuestr ? strlen(valuestr) : 0);

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
  
  /* Remove redundant separators at the end of the entry. */
  while (buf->len > 1 && buf->data[buf->len - 2] == '\n')
    buf->len--;

  svn_stringbuf_appendbytes(buf, "\f\n", 2);
}

/* Append a single entry ENTRY as an XML element to the string OUTPUT,
   using the entry for "this dir" THIS_DIR for
   comparison/optimization.  Allocations are done in POOL.  */
static void
write_entry_xml(svn_stringbuf_t **output,
                svn_wc_entry_t *entry,
                const char *name,
                svn_wc_entry_t *this_dir,
                apr_pool_t *pool)
{
  apr_hash_t *atts = apr_hash_make(pool);
  const char *valuestr;

  /*** Create a hash that represents an entry. ***/

  assert(name);

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
  if (entry->prop_time)
    {
      apr_hash_set(atts, SVN_WC__ENTRY_ATTR_PROP_TIME, APR_HASH_KEY_STRING,
                   svn_time_to_cstring(entry->prop_time, pool));
    }

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
  
  /* Cachable props. */
  if (entry->cachable_props && *entry->cachable_props)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_CACHABLE_PROPS,
                 APR_HASH_KEY_STRING, entry->cachable_props);

  /* Present props. */
  if (entry->present_props
      && *entry->present_props)
    apr_hash_set(atts, SVN_WC__ENTRY_ATTR_PRESENT_PROPS,
                 APR_HASH_KEY_STRING, entry->present_props);

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
      if (! strcmp(name, "."))
        {
          /* By golly, if this isn't recognized as the "this dir"
             entry, and it looks like '.', we're just asking for an
             infinite recursion to happen.  Abort! */
          abort();
        }

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
                         svn_path_url_add_component(this_dir->url, 
                                                    name, pool)) == 0)
                apr_hash_set(atts, SVN_WC__ENTRY_ATTR_URL,
                             APR_HASH_KEY_STRING, NULL);
            }

          /* Avoid writing repository root if that's the same as this_dir. */
          if (entry->repos && this_dir->repos
              && strcmp(entry->repos, this_dir->repos) == 0)
            apr_hash_set(atts, SVN_WC__ENTRY_ATTR_REPOS, APR_HASH_KEY_STRING,
                         NULL);

          /* Cachable props are also inherited. */
          if (entry->cachable_props && this_dir->cachable_props
              && strcmp(entry->cachable_props, this_dir->cachable_props) == 0)
            apr_hash_set(atts, SVN_WC__ENTRY_ATTR_CACHABLE_PROPS,
                         APR_HASH_KEY_STRING, NULL);
        }
    }

  /* Append the entry onto the accumulating string. */
  svn_xml_make_open_tag_hash(output,
                             pool,
                             svn_xml_self_closing,
                             SVN_WC__ENTRIES_ENTRY,
                             atts);
}

/* Construct an entries file from the ENTRIES hash in XML format in a
   newly allocated stringbuf and return it in *OUTPUT.  Allocate the
   result in POOL.  THIS_DIR is the this_dir entry in ENTRIES.  */
static void
write_entries_xml(svn_stringbuf_t **output,
                  apr_hash_t *entries,
                  svn_wc_entry_t *this_dir,
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
  write_entry_xml(output, this_dir, SVN_WC_ENTRY_THIS_DIR, this_dir, pool);

  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      svn_wc_entry_t *this_entry;

      svn_pool_clear(subpool);

      /* Get the entry and make sure its attributes are up-to-date. */
      apr_hash_this(hi, &key, NULL, &val);
      this_entry = val;

      /* Don't rewrite the "this dir" entry! */
      if (! strcmp(key, SVN_WC_ENTRY_THIS_DIR ))
        continue;

      /* Append the entry to output */
      write_entry_xml(output, this_entry, key, this_dir, subpool);
    }

  svn_xml_make_close_tag(output, pool, SVN_WC__ENTRIES_TOPLEVEL);

  svn_pool_destroy(subpool);
}
                  
svn_error_t *
svn_wc__entries_write(apr_hash_t *entries,
                      svn_wc_adm_access_t *adm_access,
                      apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_stringbuf_t *bigstr = NULL;
  apr_file_t *outfile = NULL;
  apr_hash_index_t *hi;
  svn_wc_entry_t *this_dir;

  SVN_ERR(svn_wc__adm_write_check(adm_access));

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
  SVN_ERR(svn_wc__open_adm_file(&outfile, 
                                svn_wc_adm_access_path(adm_access),
                                SVN_WC__ADM_ENTRIES,
                                (APR_WRITE | APR_CREATE),
                                pool));

  if (svn_wc__adm_wc_format(adm_access) > SVN_WC__XML_ENTRIES_VERSION)
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      bigstr = svn_stringbuf_createf(pool, "%d\n",
                                     svn_wc__adm_wc_format(adm_access));
      /* Write out "this dir" */
      write_entry(bigstr, this_dir, SVN_WC_ENTRY_THIS_DIR, this_dir, pool);

      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          svn_wc_entry_t *this_entry;

          svn_pool_clear(subpool);

          /* Get the entry and make sure its attributes are up-to-date. */
          apr_hash_this(hi, &key, NULL, &val);
          this_entry = val;

          /* Don't rewrite the "this dir" entry! */
          if (! strcmp(key, SVN_WC_ENTRY_THIS_DIR ))
            continue;

          /* Append the entry to BIGSTR */
          write_entry(bigstr, this_entry, key, this_dir, subpool);
        }

      svn_pool_destroy(subpool);
    }
  else
    /* This is needed during cleanup of a not yet upgraded WC. */
    write_entries_xml(&bigstr, entries, this_dir, pool);

  SVN_ERR_W(svn_io_file_write_full(outfile, bigstr->data, 
                                   bigstr->len, NULL, pool),
            apr_psprintf(pool,
                         _("Error writing to '%s'"),
                         svn_path_local_style
                         (svn_wc_adm_access_path(adm_access), pool)));

  err = svn_wc__close_adm_file(outfile,
                               svn_wc_adm_access_path(adm_access),
                               SVN_WC__ADM_ENTRIES, 1, pool);

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

   POOL may be used to allocate memory referenced by ENTRIES.
 */
static void
fold_entry(apr_hash_t *entries,
           const char *name,
           apr_uint32_t modify_flags,
           svn_wc_entry_t *entry,
           apr_pool_t *pool)
{
  svn_wc_entry_t *cur_entry
    = apr_hash_get(entries, name, APR_HASH_KEY_STRING);
  
  assert(name != NULL);

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

  if (modify_flags & SVN_WC__ENTRY_MODIFY_PROP_TIME)
    cur_entry->prop_time = entry->prop_time;

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

  /* has-props flag */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_HAS_PROPS)
    cur_entry->has_props = entry->has_props;

  /* prop-mods flag */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_HAS_PROP_MODS)
    cur_entry->has_prop_mods = entry->has_prop_mods;

  /* Cachable props. */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CACHABLE_PROPS)
    cur_entry->cachable_props = (entry->cachable_props
                                 ? apr_pstrdup(pool, entry->cachable_props)
                                 : NULL);

  /* Property existence */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_PRESENT_PROPS)
    cur_entry->present_props = (entry->present_props
                                ? apr_pstrdup(pool, entry->present_props)
                                : NULL);

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

  /* Make sure the entry exists in the entries hash.  Possibly it
     already did, in which case this could have been skipped, but what
     the heck. */
  apr_hash_set(entries, cur_entry->name, APR_HASH_KEY_STRING, cur_entry);
}


void
svn_wc__entry_remove(apr_hash_t *entries, const char *name)
{
  apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);
}


/* Our general purpose intelligence module for handling scheduling
   changes to a single entry.

   Given an entryname NAME in ENTRIES, examine the caller's requested
   change in *SCHEDULE and the current state of the entry.  Possibly
   modify *SCHEDULE and *MODIFY_FLAGS so that when merged, it will
   reflect the caller's original intent.

   POOL is used for local allocations only, calling this function does not
   use POOL to allocate any memory referenced by ENTRIES.
 */
static svn_error_t *
fold_scheduling(apr_hash_t *entries,
                const char *name,
                apr_uint32_t *modify_flags,
                svn_wc_schedule_t *schedule,
                apr_pool_t *pool)
{
  svn_wc_entry_t *entry, *this_dir_entry;

  /* If we're not supposed to be bothering with this anyway...return. */
  if (! (*modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE))
    return SVN_NO_ERROR;

  /* Get the current entry */
  entry = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

  /* If we're not merging in changes, only the _add, _delete, _replace
     and _normal schedules are allowed. */
  if (*modify_flags & SVN_WC__ENTRY_MODIFY_FORCE)
    {
      switch (*schedule)
        {
        case svn_wc_schedule_add:
        case svn_wc_schedule_delete:
        case svn_wc_schedule_replace:
        case svn_wc_schedule_normal:
          /* Since we aren't merging in a change, not only are these
             schedules legal, but they are final.  */
          return SVN_NO_ERROR;

        default:
          return svn_error_create(SVN_ERR_WC_SCHEDULE_CONFLICT, NULL, NULL);
        }
    }

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
             the "deleted" flag.  Check that we are not trying to
             remove the SVN_WC_ENTRY_THIS_DIR entry as that would
             leave the entries file in an invalid state. */
          assert(entry != this_dir_entry);
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
                     apr_uint32_t modify_flags,
                     svn_boolean_t do_sync,
                     apr_pool_t *pool)
{
  apr_hash_t *entries, *entries_nohidden;
  svn_boolean_t entry_was_deleted_p = FALSE;

  /* ENTRY is rather necessary, and ENTRY->kind is required to be valid! */
  assert(entry);

  /* Load ADM_ACCESS's whole entries file. */
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, pool));
  SVN_ERR(svn_wc_entries_read(&entries_nohidden, adm_access, FALSE, pool));

  /* Ensure that NAME is valid. */
  if (name == NULL)
    name = SVN_WC_ENTRY_THIS_DIR;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
    {
      svn_wc_entry_t *entry_before, *entry_after;
      apr_uint32_t orig_modify_flags = modify_flags;
      svn_wc_schedule_t orig_schedule = entry->schedule;

      /* Keep a copy of the unmodified entry on hand. */
      entry_before = apr_hash_get(entries, name, APR_HASH_KEY_STRING);
      
      /* If scheduling changes were made, we have a special routine to
         manage those modifications. */
      SVN_ERR(fold_scheduling(entries, name, &modify_flags, 
                              &entry->schedule, pool));
      
      if (entries != entries_nohidden)
        {
          SVN_ERR(fold_scheduling(entries_nohidden, name, &orig_modify_flags,
                                  &orig_schedule, pool));
          
          /* Make certain that both folding operations had the same
             result. */
          assert(orig_modify_flags == modify_flags);
          assert(orig_schedule == entry->schedule);
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
      fold_entry(entries, name, modify_flags, entry,
                 svn_wc_adm_access_pool(adm_access));
      if (entries != entries_nohidden)
        fold_entry(entries_nohidden, name, modify_flags, entry,
                   svn_wc_adm_access_pool(adm_access));
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
  if (entry->cachable_props)
    dupentry->cachable_props = apr_pstrdup(pool, entry->cachable_props);
  if (entry->present_props)
    dupentry->present_props = apr_pstrdup(pool, entry->present_props);
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
                     apr_pool_t *pool)
{
  apr_file_t *f = NULL;
  svn_stringbuf_t *accum = svn_stringbuf_createf(pool, "%d\n",
                                                 SVN_WC__VERSION);
  svn_wc_entry_t *entry = alloc_entry(pool);

  /* Sanity check. */
  assert(! repos || svn_path_is_ancestor(repos, url));

  /* Create the entries file, which must not exist prior to this. */
  SVN_ERR(svn_wc__open_adm_file(&f, path, SVN_WC__ADM_ENTRIES,
                                (APR_WRITE | APR_CREATE | APR_EXCL), pool));

  /* Add an entry for the dir itself.  The directory has no name.  It
     might have a UUID, but otherwise only the revision and default
     ancestry are present as XML attributes, and possibly an
     'incomplete' flag if the revnum is > 0. */

  entry->kind = svn_node_dir;
  entry->url = url;
  entry->revision = initial_rev;
  entry->uuid = uuid;
  entry->repos = repos;
  if (initial_rev > 0)
    entry->incomplete = TRUE;
  /* Add cachable-props here so that it can be inherited by other entries.
   */
  entry->cachable_props = SVN_WC__CACHABLE_PROPS;

  write_entry(accum, entry, SVN_WC_ENTRY_THIS_DIR, entry, pool);

  SVN_ERR_W(svn_io_file_write_full(f, accum->data, accum->len, NULL, pool),
            apr_psprintf(pool, 
                         _("Error writing entries file for '%s'"),
                         svn_path_local_style(path, pool)));

  /* Now we have a `entries' file with exactly one entry, an entry
     for this dir.  Close the file and sync it up. */
  SVN_ERR(svn_wc__close_adm_file(f, path, SVN_WC__ADM_ENTRIES, 1, pool));

  return SVN_NO_ERROR;
}


/*--------------------------------------------------------------- */

/*** Generic Entry Walker */


/* A recursive entry-walker, helper for svn_wc_walk_entries2 */
static svn_error_t *
walker_helper(const char *dirpath,
              svn_wc_adm_access_t *adm_access,
              const svn_wc_entry_callbacks_t *walk_callbacks,
              void *walk_baton,
              svn_boolean_t show_hidden,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_wc_entry_t *dot_entry;

  SVN_ERR(svn_wc_entries_read(&entries, adm_access, show_hidden, pool));
  
  /* As promised, always return the '.' entry first. */
  dot_entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR, 
                           APR_HASH_KEY_STRING);
  if (! dot_entry)
    return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                             _("Directory '%s' has no THIS_DIR entry"),
                             svn_path_local_style(dirpath, pool));

  SVN_ERR(walk_callbacks->found_entry(dirpath, dot_entry, walk_baton, pool));

  /* Loop over each of the other entries. */
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_wc_entry_t *current_entry; 
      const char *entrypath;

      /* See if someone wants to cancel this operation. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      apr_hash_this(hi, &key, &klen, &val);
      current_entry = val;

      if (strcmp(current_entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      entrypath = svn_path_join(dirpath, key, subpool);
      SVN_ERR(walk_callbacks->found_entry(entrypath, current_entry,
                                          walk_baton, subpool));

      if (current_entry->kind == svn_node_dir)
        {
          svn_wc_adm_access_t *entry_access;
          SVN_ERR(svn_wc_adm_retrieve(&entry_access, adm_access, entrypath,
                                      subpool));
          SVN_ERR(walker_helper(entrypath, entry_access,
                                walk_callbacks, walk_baton,
                                show_hidden, cancel_func, cancel_baton,
                                subpool));
        }

      svn_pool_clear(subpool);
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* The public function */
svn_error_t *
svn_wc_walk_entries(const char *path,
                    svn_wc_adm_access_t *adm_access,
                    const svn_wc_entry_callbacks_t *walk_callbacks,
                    void *walk_baton,
                    svn_boolean_t show_hidden,
                    apr_pool_t *pool)
{
  return svn_wc_walk_entries2(path, adm_access, walk_callbacks,
                              walk_baton, show_hidden, NULL, NULL,
                              pool);
}

svn_error_t *
svn_wc_walk_entries2(const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const svn_wc_entry_callbacks_t *walk_callbacks,
                     void *walk_baton,
                     svn_boolean_t show_hidden,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  
  SVN_ERR(svn_wc_entry(&entry, path, adm_access, show_hidden, pool));

  if (! entry)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"),
                             svn_path_local_style(path, pool));

  if (entry->kind == svn_node_file)
    return walk_callbacks->found_entry(path, entry, walk_baton, pool);

  else if (entry->kind == svn_node_dir)
    return walker_helper(path, adm_access, walk_callbacks, walk_baton,
                         show_hidden, cancel_func, cancel_baton, pool);

  else
    return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                             _("'%s' has an unrecognized node kind"),
                             svn_path_local_style(path, pool));
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
      SVN_ERR(svn_wc__entry_modify(adm_access, bname, &newent,
                                   (SVN_WC__ENTRY_MODIFY_DELETED
                                    | SVN_WC__ENTRY_MODIFY_SCHEDULE
                                    | SVN_WC__ENTRY_MODIFY_FORCE),
                                   TRUE, /* sync right away */ pool));

      return SVN_NO_ERROR;
    }
  else
    return svn_error_createf(SVN_ERR_WC_PATH_FOUND, NULL,
                             _("Unexpectedly found '%s': "
                               "path is marked 'missing'"),
                             svn_path_local_style(path, pool));
}
