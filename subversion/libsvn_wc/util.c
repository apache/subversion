/*
 * util.c:  general routines defying categorization; eventually I
 *          suspect they'll end up in libsvn_subr, but don't want to
 *          pollute that right now.  Note that nothing in here is
 *          specific to working copies.
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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
#include <apr_file_io.h>
#include <apr_lib.h>
#include "svn_io.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "wc.h"   /* just for prototypes of things in this .c file */
#include "private/svn_wc_private.h"

#include "svn_private_config.h"


svn_error_t *
svn_wc__ensure_directory(const char *path,
                         apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err = svn_io_check_path(path, &kind, pool);

  if (err)
    return err;

  if (kind != svn_node_none && kind != svn_node_dir)
    {
      /* If got an error other than dir non-existence, then we can't
         ensure this directory's existence, so just return the error.
         Might happen if there's a file in the way, for example. */
      return svn_error_createf(APR_ENOTDIR, NULL,
                               _("'%s' is not a directory"),
                               svn_path_local_style(path, pool));
    }
  else if (kind == svn_node_none)
    {
      /* The dir doesn't exist, and it's our job to change that. */

      err = svn_io_dir_make(path, APR_OS_DEFAULT, pool);

      if (err && !APR_STATUS_IS_ENOENT(err->apr_err))
        {
          /* Tried to create the dir, and encountered some problem
             other than non-existence of intermediate dirs.  We can't
             ensure the desired directory's existence, so just return
             the error. */
          return err;
        }
      else if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        /* (redundant conditional and comment) */
        {
          /* Okay, so the problem is a missing intermediate
             directory.  We don't know which one, so we recursively
             back up one level and try again. */
          const char *shorter = svn_path_dirname(path, pool);

          /* Clear the error. */
          svn_error_clear(err);

          if (shorter[0] == '\0')
            {
              /* A weird and probably rare situation. */
              return svn_error_create(0, NULL,
                                      _("Unable to make any directories"));
            }
          else  /* We have a valid path, so recursively ensure it. */
            {
              err = svn_wc__ensure_directory(shorter, pool);

              if (err)
                return (err);
              else
                return svn_wc__ensure_directory(path, pool);
            }
        }

      if (err)
        return err;
    }
  else  /* No problem, the dir already existed, so just leave. */
    SVN_ERR_ASSERT(kind == svn_node_dir);

  return SVN_NO_ERROR;
}

/* Return the library version number. */
const svn_version_t *
svn_wc_version(void)
{
  SVN_VERSION_BODY;
}

svn_wc_notify_t *
svn_wc_create_notify(const char *path,
                     svn_wc_notify_action_t action,
                     apr_pool_t *pool)
{
  svn_wc_notify_t *ret = apr_palloc(pool, sizeof(*ret));
  ret->path = path;
  ret->action = action;
  ret->kind = svn_node_unknown;
  ret->mime_type = NULL;
  ret->lock = NULL;
  ret->err = SVN_NO_ERROR;
  ret->content_state = ret->prop_state = svn_wc_notify_state_unknown;
  ret->lock_state = svn_wc_notify_lock_state_unknown;
  ret->revision = SVN_INVALID_REVNUM;
  ret->changelist_name = NULL;
  ret->merge_range = NULL;
  ret->path_prefix = NULL;

  return ret;
}

/* Pool cleanup function to clear an svn_error_t *. */
static apr_status_t err_cleanup(void *data)
{
  svn_error_clear(data);

  return APR_SUCCESS;
}

svn_wc_notify_t *
svn_wc_dup_notify(const svn_wc_notify_t *notify,
                  apr_pool_t *pool)
{
  svn_wc_notify_t *ret = apr_palloc(pool, sizeof(*ret));

  *ret = *notify;

  if (ret->path)
    ret->path = apr_pstrdup(pool, ret->path);
  if (ret->mime_type)
    ret->mime_type = apr_pstrdup(pool, ret->mime_type);
  if (ret->lock)
    ret->lock = svn_lock_dup(ret->lock, pool);
  if (ret->err)
    {
      ret->err = svn_error_dup(ret->err);
      apr_pool_cleanup_register(pool, ret->err, err_cleanup,
                                apr_pool_cleanup_null);
    }
  if (ret->changelist_name)
    ret->changelist_name = apr_pstrdup(pool, ret->changelist_name);
  if (ret->merge_range)
    ret->merge_range = svn_merge_range_dup(ret->merge_range, pool);
  if (ret->path_prefix)
    ret->path_prefix = apr_pstrdup(pool, ret->path_prefix);

  return ret;
}

svn_error_t *
svn_wc_external_item_create(const svn_wc_external_item2_t **item,
                            apr_pool_t *pool)
{
  *item = apr_pcalloc(pool, sizeof(svn_wc_external_item2_t));
  return SVN_NO_ERROR;
}

svn_wc_external_item_t *
svn_wc_external_item_dup(const svn_wc_external_item_t *item,
                         apr_pool_t *pool)
{
  svn_wc_external_item_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->target_dir)
    new_item->target_dir = apr_pstrdup(pool, new_item->target_dir);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  return new_item;
}

svn_wc_external_item2_t *
svn_wc_external_item2_dup(const svn_wc_external_item2_t *item,
                          apr_pool_t *pool)
{
  svn_wc_external_item2_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->target_dir)
    new_item->target_dir = apr_pstrdup(pool, new_item->target_dir);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  return new_item;
}

void svn_wc__compat_call_notify_func(void *baton,
                                     const svn_wc_notify_t *n,
                                     apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t *nb = baton;

  if (nb->func)
    (*nb->func)(nb->baton, n->path, n->action, n->kind, n->mime_type,
                n->content_state, n->prop_state, n->revision);
}

svn_boolean_t
svn_wc_match_ignore_list(const char *str, apr_array_header_t *list,
                         apr_pool_t *pool)
{
  /* For now, we simply forward to svn_cstring_match_glob_list. In the
     future, if we support more complex ignore patterns, we would iterate
     over 'list' ourselves, and decide for each pattern how to handle
     it. */

  return svn_cstring_match_glob_list(str, list);
}

svn_error_t *
svn_wc__path_switched(const char *wc_path,
                      svn_boolean_t *switched,
                      const svn_wc_entry_t *entry,
                      apr_pool_t *pool)
{
  const char *wc_parent_path, *parent_child_url;
  const svn_wc_entry_t *parent_entry;
  svn_wc_adm_access_t *parent_adm_access;
  svn_error_t *err;

  SVN_ERR(svn_path_get_absolute(&wc_path, wc_path, pool));

  if (svn_dirent_is_root(wc_path, strlen(wc_path)))
    {
      *switched = FALSE;
      return SVN_NO_ERROR;
    }

  wc_parent_path = svn_path_dirname(wc_path, pool);
  err = svn_wc_adm_open3(&parent_adm_access, NULL, wc_parent_path, FALSE, 0,
                         NULL, NULL, pool);

  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
        {
          svn_error_clear(err);
          err = SVN_NO_ERROR;
          *switched = FALSE;
        }

      return err;
    }

  SVN_ERR(svn_wc__entry_versioned(&parent_entry, wc_parent_path,
                                  parent_adm_access, FALSE, pool));
  SVN_ERR(svn_wc_adm_close(parent_adm_access));

  /* Without complete entries (and URLs) for WC_PATH and it's parent
     we return SVN_ERR_ENTRY_MISSING_URL. */
  if (!parent_entry->url || !entry->url)
    {
      const char *no_url_path = parent_entry->url ? wc_path : wc_parent_path;
      return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                               _("Cannot find a URL for '%s'"),
                               svn_path_local_style(no_url_path, pool));
    }

  parent_child_url
    = svn_path_url_add_component(parent_entry->url,
                                 svn_path_basename(wc_path, pool), pool);
  *switched = strcmp(parent_child_url, entry->url) != 0;

  return SVN_NO_ERROR;
}

svn_wc_conflict_description_t *
svn_wc_conflict_description_create_text(const char *path,
                                        svn_wc_adm_access_t *adm_access,
                                        apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;

  conflict = apr_pcalloc(pool, sizeof(*conflict));
  conflict->path = path;
  conflict->node_kind = svn_node_file;
  conflict->kind = svn_wc_conflict_kind_text;
  conflict->access = adm_access;
  conflict->action = svn_wc_conflict_action_edit;
  conflict->reason = svn_wc_conflict_reason_edited;
  return conflict;
}

svn_wc_conflict_description_t *
svn_wc_conflict_description_create_prop(const char *path,
                                        svn_wc_adm_access_t *adm_access,
                                        svn_node_kind_t node_kind,
                                        const char *property_name,
                                        apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;

  conflict = apr_pcalloc(pool, sizeof(*conflict));
  conflict->path = path;
  conflict->node_kind = node_kind;
  conflict->kind = svn_wc_conflict_kind_property;
  conflict->access = adm_access;
  conflict->property_name = property_name;
  return conflict;
}

/* --- SVNPATCH ROUTINES --- */

#define svn_iswhitespace(c) ((c) == ' ' || (c) == '\n')

/* --- WRITING DATA ITEMS --- */
 
svn_error_t *
svn_wc_write_number(svn_stream_t *target,
                    apr_pool_t *pool,
                    apr_uint64_t number)
{
  return svn_stream_printf(target, pool, "%" APR_UINT64_T_FMT " ",
                           number);
}

svn_error_t *
svn_wc_write_string(svn_stream_t *target,
                    apr_pool_t *pool,
                    const svn_string_t *str)
{
  apr_size_t len;
  /* @c svn_stream_printf() doesn't support binary bytes.  Since
   * str->data might contain binary stuff, let's use svn_stream_write()
   * instead. */
  SVN_ERR(svn_stream_printf(target, pool, "%" APR_SIZE_T_FMT ":",
                            str->len));
  len = str->len;
  SVN_ERR(svn_stream_write(target, str->data, &len));
  SVN_ERR(svn_stream_printf(target, pool, "%s", " "));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_write_cstring(svn_stream_t *target,
                     apr_pool_t *pool,
                     const char *s)
{
  return svn_stream_printf(target, pool, "%" APR_SIZE_T_FMT ":%s ",
                           strlen(s), s);
}

svn_error_t *
svn_wc_write_word(svn_stream_t *target,
                  apr_pool_t *pool,
                  const char *word)
{
  return svn_stream_printf(target, pool, "%s ", word);
}

svn_error_t *
svn_wc_write_proplist(svn_stream_t *target,
                      apr_hash_t *props,
                      apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  const char *propname;
  svn_string_t *propval;

  if (props)
    {
      iterpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
        {
          svn_pool_clear(iterpool);
          apr_hash_this(hi, &key, NULL, &val);
          propname = key;
          propval = val;          
          SVN_ERR(svn_wc_write_tuple(target, iterpool, "cs", propname,
                                     propval));
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_start_list(svn_stream_t *target)
{
  apr_size_t len = 2;
  return svn_stream_write(target, "( ", &len);
}

svn_error_t *
svn_wc_end_list(svn_stream_t *target)
{
  apr_size_t len = 2;
  return svn_stream_write(target, ") ", &len);
}

/* --- WRITING TUPLES --- */

static svn_error_t *
vwrite_tuple(svn_stream_t *target,
             apr_pool_t *pool,
             const char *fmt,
             va_list ap)
{
  svn_boolean_t opt = FALSE;
  svn_revnum_t rev;
  const char *cstr;
  const svn_string_t *str;

  if (*fmt == '!')
    fmt++;
  else
    SVN_ERR(svn_wc_start_list(target));
  for (; *fmt; fmt++)
    {
      if (*fmt == 'n' && !opt)
        SVN_ERR(svn_wc_write_number(target, pool,
                                    va_arg(ap, apr_uint64_t)));
      else if (*fmt == 'r')
        {
          rev = va_arg(ap, svn_revnum_t);
          assert(opt || SVN_IS_VALID_REVNUM(rev));
          if (SVN_IS_VALID_REVNUM(rev))
            SVN_ERR(svn_wc_write_number(target, pool, rev));
        }
      else if (*fmt == 's')
        {
          str = va_arg(ap, const svn_string_t *);
          assert(opt || str);
          if (str)
            SVN_ERR(svn_wc_write_string(target, pool, str));
        }
      else if (*fmt == 'c')
        {
          cstr = va_arg(ap, const char *);
          assert(opt || cstr);
          if (cstr)
            SVN_ERR(svn_wc_write_cstring(target, pool, cstr));
        }
      else if (*fmt == 'w')
        {
          cstr = va_arg(ap, const char *);
          assert(opt || cstr);
          if (cstr)
            SVN_ERR(svn_wc_write_word(target, pool, cstr));
        }
      else if (*fmt == 'b' && !opt)
        {
          cstr = va_arg(ap, svn_boolean_t) ? "true" : "false";
          SVN_ERR(svn_wc_write_word(target, pool, cstr));
        }
      else if (*fmt == '?')
        opt = TRUE;
      else if (*fmt == '(' && !opt)
        SVN_ERR(svn_wc_start_list(target));
      else if (*fmt == ')')
        {
          SVN_ERR(svn_wc_end_list(target));
          opt = FALSE;
        }
      else if (*fmt == '!' && !*(fmt + 1))
        return SVN_NO_ERROR;
      else
        abort();
    }
  SVN_ERR(svn_wc_end_list(target));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_write_tuple(svn_stream_t *target,
                   apr_pool_t *pool,
                   const char *fmt, ...)
{
  va_list ap;
  svn_error_t *err;

  va_start(ap, fmt);
  err = vwrite_tuple(target, pool, fmt, ap);
  va_end(ap);
  return err;
}

svn_error_t *
svn_wc_write_cmd(svn_stream_t *target,
                 apr_pool_t *pool,
                 const char *cmdname,
                 const char *fmt, ...)
{
  va_list ap;
  svn_error_t *err;

  SVN_ERR(svn_wc_start_list(target));
  SVN_ERR(svn_wc_write_word(target, pool, cmdname));
  va_start(ap, fmt);
  err = vwrite_tuple(target, pool, fmt, ap);
  va_end(ap);
  if (err)
    return err;
  SVN_ERR(svn_wc_end_list(target));

  return SVN_NO_ERROR;
}

static svn_error_t *
readbuf_getchar_skip_whitespace(svn_stream_t *from,
                                char *result)
{
  apr_size_t lenone = 1;
  do
    SVN_ERR(svn_stream_read(from, result, &lenone));
  while (svn_iswhitespace(*result));
  return SVN_NO_ERROR;
}

static svn_error_t *
read_string(svn_stream_t *from,
            apr_pool_t *pool,
            svn_ra_svn_item_t *item,
            apr_uint64_t len)
{
  char readbuf[4096];
  apr_size_t readbuf_len;
  svn_stringbuf_t *stringbuf = svn_stringbuf_create("", pool);

  /* We can't store strings longer than the maximum size of apr_size_t,
   * so check for wrapping */
  if (((apr_size_t) len) < len) 
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("String length larger than maximum"));

  while (len)
    {
      readbuf_len = len > sizeof(readbuf) ? sizeof(readbuf) : len;

      SVN_ERR(svn_stream_read(from, readbuf, &readbuf_len));
      /* Read into a stringbuf_t to so we don't allow the sender to allocate
       * an arbitrary amount of memory without actually sending us that much
       * data */
      svn_stringbuf_appendbytes(stringbuf, readbuf, readbuf_len);
      len -= readbuf_len;
    }
  
  item->kind = SVN_RA_SVN_STRING;
  item->u.string = apr_palloc(pool, sizeof(*item->u.string));
  item->u.string->data = stringbuf->data;
  item->u.string->len = stringbuf->len;

  return SVN_NO_ERROR; 
}


static svn_error_t *
read_item(svn_stream_t *from,
          apr_pool_t *pool,
          svn_ra_svn_item_t *item,
          char first_char,
          int level)
{
  char c = first_char;
  apr_uint64_t val, prev_val=0;
  svn_stringbuf_t *str;
  svn_ra_svn_item_t *listitem;
  apr_size_t lenone = 1;

  if (++level >= 64)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Too many nested items"));
  
  
  /* Determine the item type and read it in.  Make sure that c is the
   * first character at the end of the item so we can test to make
   * sure it's whitespace. */
  if (apr_isdigit(c))
    {
      /* It's a number or a string.  Read the number part, either way. */
      val = c - '0';
      while (1)
        {
          prev_val = val;
          SVN_ERR(svn_stream_read(from, &c, &lenone));
          if (!apr_isdigit(c))
            break;
          val = val * 10 + (c - '0');
          if ((val / 10) != prev_val) /* val wrapped past maximum value */
            return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                    _("Number is larger than maximum"));
        }
      if (c == ':')
        {
          /* It's a string. */
          SVN_ERR(read_string(from, pool, item, val));
          SVN_ERR(svn_stream_read(from, &c, &lenone));
        }
      else
        {
          /* It's a number. */
          item->kind = SVN_RA_SVN_NUMBER;
          item->u.number = val;
        }
    }
  else if (apr_isalpha(c))
    {
      /* It's a word. */
      str = svn_stringbuf_ncreate(&c, 1, pool);
      while (1)
        {
          SVN_ERR(svn_stream_read(from, &c, &lenone));
          if (!apr_isalnum(c) && c != '-')
            break;
          svn_stringbuf_appendbytes(str, &c, 1);
        }
      item->kind = SVN_RA_SVN_WORD;
      item->u.word = str->data;
    }
  else if (c == '(')
    {
      /* Read in the list items. */
      item->kind = SVN_RA_SVN_LIST;
      item->u.list = apr_array_make(pool, 0, sizeof(svn_ra_svn_item_t));
      while (1)
        {
          SVN_ERR(readbuf_getchar_skip_whitespace(from, &c));
          if (c == ')')
            break;
          listitem = apr_array_push(item->u.list);
          SVN_ERR(read_item(from, pool, listitem, c, level));
        }
      SVN_ERR(svn_stream_read(from, &c, &lenone));
    }

  if (!svn_iswhitespace(c))
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Malformed patch data"));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_read_item(svn_stream_t *from,
                 apr_pool_t *pool,
                 svn_ra_svn_item_t **item)
{
  char c;

  /* Allocate space, read the first character, and then do the rest of
   * the work.  This makes sense because of the way lists are read. */
  *item = apr_palloc(pool, sizeof(**item));
  SVN_ERR(readbuf_getchar_skip_whitespace(from, &c));
  return read_item(from, pool, *item, c, 0);
}

/* Parse a tuple of svn_ra_svn_item_t *'s.  Advance *FMT to the end of the
 * tuple specification and advance AP by the corresponding arguments. */
static svn_error_t *
vparse_tuple(apr_array_header_t *items,
             apr_pool_t *pool,
             const char **fmt,
             va_list *ap)
{
  int count, nesting_level;
  svn_ra_svn_item_t *elt;

  for (count = 0; **fmt && count < items->nelts; (*fmt)++, count++)
    {
      /* '?' just means the tuple may stop; skip past it. */
      if (**fmt == '?')
        (*fmt)++;
      elt = &APR_ARRAY_IDX(items, count, svn_ra_svn_item_t);
      if (**fmt == 'n' && elt->kind == SVN_RA_SVN_NUMBER)
        *va_arg(*ap, apr_uint64_t *) = elt->u.number;
      else if (**fmt == 'r' && elt->kind == SVN_RA_SVN_NUMBER)
        *va_arg(*ap, svn_revnum_t *) = elt->u.number;
      else if (**fmt == 's' && elt->kind == SVN_RA_SVN_STRING)
        *va_arg(*ap, svn_string_t **) = elt->u.string;
      else if (**fmt == 'c' && elt->kind == SVN_RA_SVN_STRING)
        *va_arg(*ap, const char **) = elt->u.string->data;
      else if (**fmt == 'w' && elt->kind == SVN_RA_SVN_WORD)
        *va_arg(*ap, const char **) = elt->u.word;
      else if (**fmt == 'b' && elt->kind == SVN_RA_SVN_WORD)
        {
          if (strcmp(elt->u.word, "true") == 0)
            *va_arg(*ap, svn_boolean_t *) = TRUE;
          else if (strcmp(elt->u.word, "false") == 0)
            *va_arg(*ap, svn_boolean_t *) = FALSE;
          else
            break;
        }
      else if (**fmt == 'B' && elt->kind == SVN_RA_SVN_WORD)
        {
          if (strcmp(elt->u.word, "true") == 0)
            *va_arg(*ap, apr_uint64_t *) = TRUE;
          else if (strcmp(elt->u.word, "false") == 0)
            *va_arg(*ap, apr_uint64_t *) = FALSE;
          else
            break;
        }
      else if (**fmt == 'l' && elt->kind == SVN_RA_SVN_LIST)
        *va_arg(*ap, apr_array_header_t **) = elt->u.list;
      else if (**fmt == '(' && elt->kind == SVN_RA_SVN_LIST)
        {
          (*fmt)++;
          SVN_ERR(vparse_tuple(elt->u.list, pool, fmt, ap));
        }
      else if (**fmt == ')')
        return SVN_NO_ERROR;
      else
        break;
    }
  if (**fmt == '?')
    {
      nesting_level = 0;
      for (; **fmt; (*fmt)++)
        {
          switch (**fmt)
            {
            case '?':
              break;
            case 'r':
              *va_arg(*ap, svn_revnum_t *) = SVN_INVALID_REVNUM;
              break;
            case 's':
              *va_arg(*ap, svn_string_t **) = NULL;
              break;
            case 'c':
            case 'w':
              *va_arg(*ap, const char **) = NULL;
              break;
            case 'l':
              *va_arg(*ap, apr_array_header_t **) = NULL;
              break;
            case 'B':
            case 'n':
              *va_arg(*ap, apr_uint64_t *) = SVN_RA_SVN_UNSPECIFIED_NUMBER;
              break;
            case '(':
              nesting_level++;
              break;
            case ')':
              if (--nesting_level < 0)
                return SVN_NO_ERROR;
              break;
            default:
              abort();
            }
        }
    }
  if (**fmt && **fmt != ')')
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Malformed patch data"));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_parse_tuple(apr_array_header_t *list,
                   apr_pool_t *pool,
                   const char *fmt, ...)
{
  svn_error_t *err;
  va_list ap;

  va_start(ap, fmt);
  err = vparse_tuple(list, pool, &fmt, &ap);
  va_end(ap);
  return err;
}

svn_error_t *
svn_wc_read_tuple(svn_stream_t *from,
                  apr_pool_t *pool,
                  const char *fmt, ...)
{
  va_list ap;
  svn_ra_svn_item_t *item;
  svn_error_t *err;

  SVN_ERR(svn_wc_read_item(from, pool, &item));
  if (item->kind != SVN_RA_SVN_LIST)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Malformed patch data"));
  va_start(ap, fmt);
  err = vparse_tuple(item->u.list, pool, &fmt, &ap);
  va_end(ap);
  return err;
}
