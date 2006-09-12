/*
 * xml.c :  standard XML parsing routines for ra_serf
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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



#include <apr_uri.h>

#include <expat.h>

#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "ra_serf.h"


void
svn_ra_serf__define_ns(svn_ra_serf__ns_t **ns_list,
                       const char **attrs,
                       apr_pool_t *pool)
{
  const char **tmp_attrs = attrs;

  while (*tmp_attrs)
    {
      if (strncmp(*tmp_attrs, "xmlns", 5) == 0)
        {
          svn_ra_serf__ns_t *new_ns, *cur_ns;
          int found = 0;

          /* Have we already defined this ns previously? */
          for (cur_ns = *ns_list; cur_ns; cur_ns = cur_ns->next)
            {
              if (strcmp(cur_ns->namespace, tmp_attrs[0] + 6) == 0)
                {
                  found = 1;
                  break;
                }
            }

          if (!found)
            {
              new_ns = apr_palloc(pool, sizeof(*new_ns));
              new_ns->namespace = apr_pstrdup(pool, tmp_attrs[0] + 6);
              new_ns->url = apr_pstrdup(pool, tmp_attrs[1]);

              new_ns->next = *ns_list;

              *ns_list = new_ns;
            }
        }
      tmp_attrs += 2;
    }
}

/*
 * Look up NAME in the NS_LIST list for previously declared namespace
 * definitions and return a DAV_PROPS_T-tuple that has values.
 */
svn_ra_serf__dav_props_t
svn_ra_serf__expand_ns(svn_ra_serf__ns_t *ns_list,
                       const char *name)
{
  const char *colon;
  svn_ra_serf__dav_props_t prop_name;

  colon = strchr(name, ':');
  if (colon)
    {
      svn_ra_serf__ns_t *ns;

      prop_name.namespace = NULL;

      for (ns = ns_list; ns; ns = ns->next)
        {
          if (strncmp(ns->namespace, name, colon - name) == 0)
            {
              prop_name.namespace = ns->url;
              break;
            }
        }

      if (!prop_name.namespace)
        {
          abort();
        }

      prop_name.name = colon + 1;
    }
  else
    {
      /* use default namespace for now */
      prop_name.namespace = "";
      prop_name.name = name;
    }

  return prop_name;
}

/*
 * Look for ATTR_NAME in the attrs array and return its value.
 *
 * Returns NULL if no matching name is found.
 */
const char *
svn_ra_serf__find_attr(const char **attrs,
                       const char *attr_name)
{
  const char *attr_val = NULL;
  const char **tmp_attrs = attrs;

  while (*tmp_attrs)
    {
      if (strcmp(*tmp_attrs, attr_name) == 0)
        {
          attr_val = *(tmp_attrs + 1);
          break;
        }
      tmp_attrs += 2;
    }

  return attr_val;
}

void
svn_ra_serf__expand_string(const char **cur, apr_size_t *cur_len,
                           const char *new, apr_size_t new_len,
                           apr_pool_t *pool)
{
  if (!*cur)
    {
      *cur = apr_pstrmemdup(pool, new, new_len);
      *cur_len = new_len;
    }
  else
    {
      char *new_cur;

      /* append the data we received before. */
      new_cur = apr_palloc(pool, *cur_len+new_len+1);

      memcpy(new_cur, *cur, *cur_len);
      memcpy(new_cur + *cur_len, new, new_len);

      /* NULL-term our new string */
      new_cur[*cur_len + new_len] = '\0';

      /* update our length */
      *cur_len += new_len;
      *cur = new_cur;
    }
}

void svn_ra_serf__add_tag_buckets(serf_bucket_t *agg_bucket, const char *tag,
                                  const char *value,
                                  serf_bucket_alloc_t *bkt_alloc)
{
  serf_bucket_t *tmp;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(tag, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(">", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  if (value)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING(value, bkt_alloc);
      serf_bucket_aggregate_append(agg_bucket, tmp);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</", 2, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(tag, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(">", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);
}

void
svn_ra_serf__xml_push_state(svn_ra_serf__xml_parser_t *parser,
                            int state)
{
  svn_ra_serf__xml_state_t *new_state;

  if (!parser->free_state)
    {
      new_state = apr_palloc(parser->pool, sizeof(*new_state));
      apr_pool_create(&new_state->pool, parser->pool);
    }
  else
    {
      new_state = parser->free_state;
      parser->free_state = parser->free_state->prev;

      apr_pool_clear(new_state->pool);
    }

  if (parser->state)
    {
      new_state->private = parser->state->private;
      new_state->ns_list = parser->state->ns_list;
    }
  else
    {
      new_state->private = NULL;
      new_state->ns_list = NULL;
    }

  new_state->current_state = state;

  /* Add it to the state chain. */
  new_state->prev = parser->state;
  parser->state = new_state;
}

void svn_ra_serf__xml_pop_state(svn_ra_serf__xml_parser_t *parser)
{
  svn_ra_serf__xml_state_t *cur_state;

  cur_state = parser->state;
  parser->state = cur_state->prev;
  cur_state->prev = parser->free_state;
  parser->free_state = cur_state;
}
