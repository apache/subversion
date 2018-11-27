/*
 * list.c: mod_dav_svn REPORT handler for recursive directory listings
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
#include <apr_strings.h>
#include <apr_xml.h>

#include <mod_dav.h>

#include "svn_repos.h"
#include "svn_ctype.h"
#include "svn_string.h"
#include "svn_types.h"
#include "svn_base64.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_dav.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_time.h"

#include "private/svn_log.h"
#include "private/svn_fspath.h"

#include "../dav_svn.h"

/* Baton type to be used with list_receiver. */
typedef struct list_receiver_baton_t
{
  /* this buffers the output for a bit and is automatically flushed,
     at appropriate times, by the Apache filter system. */
  apr_bucket_brigade *bb;

  /* where to deliver the output */
  dav_svn__output *output;

  /* Whether we've written the <S:log-report> header.  Allows for lazy
     writes to support mod_dav-based error handling. */
  svn_boolean_t needs_header;

  /* Are we talking to a SVN client? */
  svn_boolean_t is_svn_client;

  /* Helper variables to force early bucket brigade flushes */
  int result_count;
  int next_forced_flush;

  /* Send the field selected by these flags. */
  apr_uint32_t dirent_fields;
} list_receiver_baton_t;


/* If LRB->needs_header is true, send the "<S:list-report>" start
   element and set LRB->needs_header to zero.  Else do nothing.
   This is basically duplicated in file_revs.c.  Consider factoring if
   duplicating again. */
static svn_error_t *
maybe_send_header(list_receiver_baton_t *lrb)
{
  if (lrb->needs_header)
    {
      SVN_ERR(dav_svn__brigade_puts(lrb->bb, lrb->output,
                                    DAV_XML_HEADER DEBUG_CR
                                    "<S:list-report xmlns:S=\""
                                    SVN_XML_NAMESPACE "\" "
                                    "xmlns:D=\"DAV:\">" DEBUG_CR));
      lrb->needs_header = FALSE;
    }

  return SVN_NO_ERROR;
}


/* Implements svn_repos_dirent_receiver_t, sending DIRENT and PATH to the
 * client.  BATON must be a list_receiver_baton_t. */
static svn_error_t *
list_receiver(const char *path,
              svn_dirent_t *dirent,
              void *baton,
              apr_pool_t *pool)
{
  list_receiver_baton_t *b = baton;
  const char *kind = (b->dirent_fields & SVN_DIRENT_KIND)
                   ? svn_node_kind_to_word(dirent->kind)
                   : "unknown";
  const char *attr_size = "";
  const char *attr_has_props = "";
  const char *attr_created_rev = "";
  const char *attr_date = "";
  const char *tag_author = "";

  if (b->dirent_fields & SVN_DIRENT_SIZE)
    attr_size = apr_psprintf(pool, " size=\"%" SVN_FILESIZE_T_FMT "\"",
                             dirent->size);

  if (b->dirent_fields & SVN_DIRENT_HAS_PROPS)
    attr_has_props = dirent->has_props
                   ? " has-props=\"true\""
                   : " has-props=\"false\"";

  if (b->dirent_fields & SVN_DIRENT_CREATED_REV)
    attr_created_rev = apr_psprintf(pool, " created-rev=\"%ld\"",
                                    dirent->created_rev);

  if (b->dirent_fields & SVN_DIRENT_TIME)
    {
      const char *ctime = svn_time_to_cstring(dirent->time, pool);
      attr_date = apr_psprintf(pool, " date=\"%s\"",
                               apr_xml_quote_string(pool, ctime, 0));
    }

  if ((b->dirent_fields & SVN_DIRENT_LAST_AUTHOR) && dirent->last_author)
    {
      const char *author = dav_svn__fuzzy_escape_author(dirent->last_author,
                                                        b->is_svn_client,
                                                        pool, pool);
      tag_author = apr_psprintf(pool,
                                "<D:creator-displayname>%s"
                                "</D:creator-displayname>",
                                apr_xml_quote_string(pool, author, 1));
    }

  SVN_ERR(maybe_send_header(b));
 
  /* If we need to close the element, then send the attributes
     that apply to all changed items and then close the element. */
  SVN_ERR(dav_svn__brigade_printf(b->bb, b->output,
                                 "<S:item"
                                 " node-kind=\"%s\""
                                 "%s"
                                 "%s"
                                 "%s"
                                 "%s>%s%s</S:item>" DEBUG_CR,
                                 kind,
                                 attr_size,
                                 attr_has_props,
                                 attr_created_rev,
                                 attr_date,
                                 apr_xml_quote_string(pool, path, 0),
                                 tag_author));

  /* In general APR will flush the brigade every 8000 bytes through the filter
     stack, but log items may not be generated that fast, especially in
     combination with authz and busy servers. We now explictly flush after
     direntry 4, 16, 64 and 256 to produce a few results fast.

     This introduces 4 full flushes of our brigade and the installed output
     filters at growing intervals and then falls back to the standard
     buffering of 8000 bytes + whatever buffers are added in output filters. */
  b->result_count++;
  if (b->result_count == b->next_forced_flush)
    {
      apr_bucket *bkt;

      /* Compared to using ap_filter_flush(), which we use in other place
         this adds a flush frame before flushing the brigade, to make output
         filters perform a flush as well */

      /* No brigade empty check. We want output filters to flush anyway */
      bkt = apr_bucket_flush_create(
                dav_svn__output_get_bucket_alloc(b->output));
      APR_BRIGADE_INSERT_TAIL(b->bb, bkt);
      SVN_ERR(dav_svn__output_pass_brigade(b->output, b->bb));

      if (b->result_count < 256)
        b->next_forced_flush = b->next_forced_flush * 4;
    }

  return SVN_NO_ERROR;
}

dav_error *
dav_svn__list_report(const dav_resource *resource,
                     const apr_xml_doc *doc,
                     dav_svn__output *output)
{
  svn_error_t *serr;
  dav_error *derr = NULL;
  apr_xml_elem *child;
  list_receiver_baton_t lrb = { 0 };
  dav_svn__authz_read_baton arb;
  const dav_svn_repos *repos = resource->info->repos;
  int ns;
  const char *full_path;
  svn_boolean_t path_info_only;
  svn_fs_root_t *root;
  svn_depth_t depth = svn_depth_unknown;

  /* These get determined from the request document. */
  svn_revnum_t rev = SVN_INVALID_REVNUM;     /* defaults to HEAD */
  apr_array_header_t *patterns = NULL;

  /* Sanity check. */
  if (!resource->info->repos_path)
    return dav_svn__new_error(resource->pool, HTTP_BAD_REQUEST, 0, 0,
                              "The request does not specify a repository path");
  ns = dav_svn__find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    {
      return dav_svn__new_error_svn(resource->pool, HTTP_BAD_REQUEST, 0, 0,
                                    "The request does not contain the 'svn:' "
                                    "namespace, so it is not going to have "
                                    "certain required elements");
    }

  for (child = doc->root->first_child; child != NULL; child = child->next)
    {
      /* if this element isn't one of ours, then skip it */
      if (child->ns != ns)
        continue;

      else if (strcmp(child->name, "path") == 0)
        {
          const char *rel_path = dav_xml_get_cdata(child, resource->pool, 0);
          if ((derr = dav_svn__test_canonical(rel_path, resource->pool)))
            return derr;

          /* Force REL_PATH to be a relative path, not an fspath. */
          rel_path = svn_relpath_canonicalize(rel_path, resource->pool);

          /* Append the REL_PATH to the base FS path to get an
             absolute repository path. */
          full_path = svn_fspath__join(resource->info->repos_path, rel_path,
                                       resource->pool);
        }
      else if (strcmp(child->name, "revision") == 0)
        rev = SVN_STR_TO_REV(dav_xml_get_cdata(child, resource->pool, 1));
      else if (strcmp(child->name, "depth") == 0)
        depth = svn_depth_from_word(dav_xml_get_cdata(child, resource->pool, 1));
      else if (strcmp(child->name, "no-patterns") == 0)
        {
          /* specified but empty pattern list */
          patterns = apr_array_make(resource->pool, 0, sizeof(const char *));
        }
      else if (strcmp(child->name, "pattern") == 0)
        {
          const char *name = dav_xml_get_cdata(child, resource->pool, 0);
          if (!patterns)
            patterns = apr_array_make(resource->pool, 1, sizeof(const char *));
          APR_ARRAY_PUSH(patterns, const char *) = name;
        }
      else if (strcmp(child->name, "prop") == 0)
        {
          const char *name = dav_xml_get_cdata(child, resource->pool, 0);
          if (strcmp(name, "DAV:resourcetype") == 0)
            lrb.dirent_fields |= SVN_DIRENT_KIND;
          else if (strcmp(name, "DAV:getcontentlength") == 0)
            lrb.dirent_fields |= SVN_DIRENT_SIZE;
          else if (strcmp(name, SVN_DAV_PROP_NS_DAV "deadprop-count") == 0)
            lrb.dirent_fields |= SVN_DIRENT_HAS_PROPS;
          else if (strcmp(name, "DAV:" SVN_DAV__VERSION_NAME) == 0)
            lrb.dirent_fields |= SVN_DIRENT_CREATED_REV;
          else if (strcmp(name, "DAV:" SVN_DAV__CREATIONDATE) == 0)
            lrb.dirent_fields |= SVN_DIRENT_TIME;
          else if (strcmp(name, "DAV:creator-displayname") == 0)
            lrb.dirent_fields |= SVN_DIRENT_LAST_AUTHOR;
          else if (strcmp(name, "DAV:allprop") == 0)
            lrb.dirent_fields |= SVN_DIRENT_ALL;
        }
      /* else unknown element; skip it */
    }

  /* Build authz read baton */
  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  /* Build log receiver baton */
  lrb.bb = apr_brigade_create(resource->pool,  /* not the subpool! */
                              dav_svn__output_get_bucket_alloc(output));
  lrb.output = output;
  lrb.needs_header = TRUE;
  lrb.next_forced_flush = 4;
  lrb.is_svn_client = resource->info->repos->is_svn_client;

  /* Fetch the root of the appropriate revision. */
  serr = svn_fs_revision_root(&root, repos->fs, rev, resource->pool);
  if (!serr)
    {
      /* Fetch the directory entries if requested and send them immediately. */
      path_info_only = (lrb.dirent_fields & ~SVN_DIRENT_KIND) == 0;
      serr = svn_repos_list(root, full_path, patterns, depth, path_info_only,
                            dav_svn__authz_read_func(&arb), &arb,
                            list_receiver, &lrb, NULL, NULL, resource->pool);
    }

  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, NULL,
                                  resource->pool);
      goto cleanup;
    }

  if ((serr = maybe_send_header(&lrb)))
    {
      derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                  "Error beginning REPORT response.",
                                  resource->pool);
      goto cleanup;
    }

  if ((serr = dav_svn__brigade_puts(lrb.bb, lrb.output,
                                    "</S:list-report>" DEBUG_CR)))
    {
      derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                  "Error ending REPORT response.",
                                  resource->pool);
      goto cleanup;
    }

 cleanup:

  dav_svn__operational_log(resource->info,
                           svn_log__list(full_path, rev, patterns, depth,
                                         lrb.dirent_fields, resource->pool));

  return dav_svn__final_flush_or_error(resource->info->r, lrb.bb, output,
                                       derr, resource->pool);
}
