/*
 * properties.c:  stuff related to Subversion properties
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <string.h>       /* for strncmp() */
#include "svn_string.h"
#include "svn_props.h"
#include "svn_error.h"
#include "svn_ctype.h"


svn_boolean_t
svn_prop_is_svn_prop(const char *prop_name)
{
  return strncmp(prop_name, SVN_PROP_PREFIX, (sizeof(SVN_PROP_PREFIX) - 1))
         == 0;
}


svn_boolean_t
svn_prop_has_svn_prop(const apr_hash_t *props, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const void *prop_name;

  if (! props)
    return FALSE;

  for (hi = apr_hash_first(pool, (apr_hash_t *)props); hi;
       hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &prop_name, NULL, NULL);
      if (svn_prop_is_svn_prop((const char *) prop_name))
        return TRUE;
    }

  return FALSE;
}


#define SIZEOF_WC_PREFIX (sizeof(SVN_PROP_WC_PREFIX) - 1)
#define SIZEOF_ENTRY_PREFIX (sizeof(SVN_PROP_ENTRY_PREFIX) - 1)

svn_prop_kind_t
svn_property_kind2(const char *prop_name)
{

  if (strncmp(prop_name, SVN_PROP_WC_PREFIX, SIZEOF_WC_PREFIX) == 0)
    return svn_prop_wc_kind;

  if (strncmp(prop_name, SVN_PROP_ENTRY_PREFIX, SIZEOF_ENTRY_PREFIX) == 0)
    return svn_prop_entry_kind;

  return svn_prop_regular_kind;
}


/* NOTE: this function is deprecated, but we cannot move it to deprecated.c
   because we need the SIZEOF_*_PREFIX constant symbols defined above.  */
svn_prop_kind_t
svn_property_kind(int *prefix_len,
                  const char *prop_name)
{
  svn_prop_kind_t kind = svn_property_kind2(prop_name);

  if (prefix_len)
    {
      if (kind == svn_prop_wc_kind)
        *prefix_len = SIZEOF_WC_PREFIX;
      else if (kind == svn_prop_entry_kind)
        *prefix_len = SIZEOF_ENTRY_PREFIX;
      else
        *prefix_len = 0;
    }

  return kind;
}


svn_error_t *
svn_categorize_props(const apr_array_header_t *proplist,
                     apr_array_header_t **entry_props,
                     apr_array_header_t **wc_props,
                     apr_array_header_t **regular_props,
                     apr_pool_t *pool)
{
  int i;
  if (entry_props)
    *entry_props = apr_array_make(pool, 1, sizeof(svn_prop_t));
  if (wc_props)
    *wc_props = apr_array_make(pool, 1, sizeof(svn_prop_t));
  if (regular_props)
    *regular_props = apr_array_make(pool, 1, sizeof(svn_prop_t));

  for (i = 0; i < proplist->nelts; i++)
    {
      svn_prop_t *prop, *newprop;
      enum svn_prop_kind kind;

      prop = &APR_ARRAY_IDX(proplist, i, svn_prop_t);
      kind = svn_property_kind2(prop->name);
      newprop = NULL;

      if (kind == svn_prop_regular_kind)
        {
          if (regular_props)
            newprop = apr_array_push(*regular_props);
        }
      else if (kind == svn_prop_wc_kind)
        {
          if (wc_props)
            newprop = apr_array_push(*wc_props);
        }
      else if (kind == svn_prop_entry_kind)
        {
          if (entry_props)
            newprop = apr_array_push(*entry_props);
        }
      else
        /* Technically this can't happen, but might as well have the
           code ready in case that ever changes. */
        return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                                 "Bad property kind for property '%s'",
                                 prop->name);

      if (newprop)
        {
          newprop->name = prop->name;
          newprop->value = prop->value;
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_prop_diffs(apr_array_header_t **propdiffs,
               apr_hash_t *target_props,
               apr_hash_t *source_props,
               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *ary = apr_array_make(pool, 1, sizeof(svn_prop_t));

  /* Note: we will be storing the pointers to the keys (from the hashes)
     into the propdiffs array.  It is acceptable for us to
     reference the same memory as the base/target_props hash. */

  /* Loop over SOURCE_PROPS and examine each key.  This will allow us to
     detect any `deletion' events or `set-modification' events.  */
  for (hi = apr_hash_first(pool, source_props); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_string_t *propval1, *propval2;

      /* Get next property */
      apr_hash_this(hi, &key, &klen, &val);
      propval1 = val;

      /* Does property name exist in TARGET_PROPS? */
      propval2 = apr_hash_get(target_props, key, klen);

      if (propval2 == NULL)
        {
          /* Add a delete event to the array */
          svn_prop_t *p = apr_array_push(ary);
          p->name = key;
          p->value = NULL;
        }
      else if (! svn_string_compare(propval1, propval2))
        {
          /* Add a set (modification) event to the array */
          svn_prop_t *p = apr_array_push(ary);
          p->name = key;
          p->value = svn_string_dup(propval2, pool);
        }
    }

  /* Loop over TARGET_PROPS and examine each key.  This allows us to
     detect `set-creation' events */
  for (hi = apr_hash_first(pool, target_props); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_string_t *propval;

      /* Get next property */
      apr_hash_this(hi, &key, &klen, &val);
      propval = val;

      /* Does property name exist in SOURCE_PROPS? */
      if (NULL == apr_hash_get(source_props, key, klen))
        {
          /* Add a set (creation) event to the array */
          svn_prop_t *p = apr_array_push(ary);
          p->name = key;
          p->value = svn_string_dup(propval, pool);
        }
    }

  /* Done building our array of user events. */
  *propdiffs = ary;

  return SVN_NO_ERROR;
}

/**
 * Reallocate the members of PROP using POOL.
 */
static void
svn_prop__members_dup(svn_prop_t *prop, apr_pool_t *pool)
{
  if (prop->name)
    prop->name = apr_pstrdup(pool, prop->name);
  if (prop->value)
    prop->value = svn_string_dup(prop->value, pool);
}

svn_prop_t *
svn_prop_dup(const svn_prop_t *prop, apr_pool_t *pool)
{
  svn_prop_t *new_prop = apr_palloc(pool, sizeof(*new_prop));

  *new_prop = *prop;

  svn_prop__members_dup(new_prop, pool);

  return new_prop;
}

apr_array_header_t *
svn_prop_array_dup(const apr_array_header_t *array, apr_pool_t *pool)
{
  int i;
  apr_array_header_t *new_array = apr_array_copy(pool, array);
  for (i = 0; i < new_array->nelts; ++i)
    {
      svn_prop_t *elt = &APR_ARRAY_IDX(new_array, i, svn_prop_t);
      svn_prop__members_dup(elt, pool);
    }
  return new_array;
}

apr_array_header_t *
svn_prop_hash_to_array(apr_hash_t *hash, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *array = apr_array_make(pool, apr_hash_count(hash),
                                             sizeof(svn_prop_t));

  for (hi = apr_hash_first(pool, hash); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      svn_prop_t prop;

      apr_hash_this(hi, &key, NULL, &val);
      prop.name = key;
      prop.value = val;
      APR_ARRAY_PUSH(array, svn_prop_t) = prop;
    }

  return array;
}

apr_hash_t *
svn_prop_hash_dup(apr_hash_t *hash,
                  apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *new_hash = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, hash); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *prop;

      apr_hash_this(hi, &key, &klen, &prop);
      apr_hash_set(new_hash, apr_pstrmemdup(pool, key, klen), klen,
                   svn_string_dup(prop, pool));
    }
  return new_hash;
}

apr_hash_t *
svn_prop_array_to_hash(const apr_array_header_t *properties,
                       apr_pool_t *pool)
{
  int i;
  apr_hash_t *prop_hash = apr_hash_make(pool);

  for (i = 0; i < properties->nelts; i++)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(properties, i, svn_prop_t);
      apr_hash_set(prop_hash, prop->name, APR_HASH_KEY_STRING, prop->value);
    }

  return prop_hash;
}

svn_boolean_t
svn_prop_is_boolean(const char *prop_name)
{
  /* If we end up with more than 3 of these, we should probably put
     them in a table and use bsearch.  With only three, it doesn't
     make any speed difference.  */
  if (strcmp(prop_name, SVN_PROP_EXECUTABLE) == 0
      || strcmp(prop_name, SVN_PROP_NEEDS_LOCK) == 0
      || strcmp(prop_name, SVN_PROP_SPECIAL) == 0)
    return TRUE;
  return FALSE;
}


svn_boolean_t
svn_prop_needs_translation(const char *propname)
{
  /* ### Someday, we may want to be picky and choosy about which
     properties require UTF8 and EOL conversion.  For now, all "svn:"
     props need it.  */

  return svn_prop_is_svn_prop(propname);
}


svn_boolean_t
svn_prop_name_is_valid(const char *prop_name)
{
  const char *p = prop_name;

  /* The characters we allow use identical representations in UTF8
     and ASCII, so we can just test for the appropriate ASCII codes.
     But we can't use standard C character notation ('A', 'B', etc)
     because there's no guarantee that this C environment is using
     ASCII. */

  if (!(svn_ctype_isalpha(*p)
        || *p == SVN_CTYPE_ASCII_COLON
        || *p == SVN_CTYPE_ASCII_UNDERSCORE))
    return FALSE;
  p++;
  for (; *p; p++)
    {
      if (!(svn_ctype_isalnum(*p)
            || *p == SVN_CTYPE_ASCII_MINUS
            || *p == SVN_CTYPE_ASCII_DOT
            || *p == SVN_CTYPE_ASCII_COLON
            || *p == SVN_CTYPE_ASCII_UNDERSCORE))
        return FALSE;
    }
  return TRUE;
}

const char *
svn_prop_get_value(apr_hash_t *props,
                   const char *prop_name)
{
  svn_string_t *str;

  if (!props)
    return NULL;

  str = apr_hash_get(props, prop_name, APR_HASH_KEY_STRING);

  if (str)
    return str->data;

  return NULL;
}

svn_prop_inherited_item_t *
svn_prop_inherited_item_dup(const svn_prop_inherited_item_t *inherited_prop,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_prop_inherited_item_t *new_iprop = apr_palloc(result_pool,
                                                    sizeof(*new_iprop));

  new_iprop->path_or_url = apr_pstrdup(
    result_pool, inherited_prop->path_or_url);
  new_iprop->prop_hash = apr_hash_make(result_pool);

  for (hi = apr_hash_first(scratch_pool, inherited_prop->prop_hash);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_string_t *value = svn__apr_hash_index_val(hi);
      apr_hash_set(new_iprop->prop_hash,
                   apr_pstrdup(result_pool, name),
                   APR_HASH_KEY_STRING,
                   svn_string_dup(value, result_pool));
    }

  return new_iprop;
}

apr_array_header_t *
svn_prop_inherited_array_dup(const apr_array_header_t *prop_array,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  int i;
  apr_array_header_t *new_array = apr_array_make(
    result_pool, prop_array->nelts, sizeof(svn_prop_inherited_item_t *));

  for (i = 0; i < prop_array->nelts; ++i)
    {
      svn_prop_inherited_item_t *elt =
        APR_ARRAY_IDX(prop_array, i, svn_prop_inherited_item_t *);

      elt = svn_prop_inherited_item_dup(elt, result_pool, scratch_pool);
      APR_ARRAY_PUSH(new_array, svn_prop_inherited_item_t *) = elt;
    }
  return new_array;
}
