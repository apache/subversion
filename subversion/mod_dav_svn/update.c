/*
 * update.c: handle the update-report request and response
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <apr_strings.h>
#include <apr_xml.h>
#include <mod_dav.h>

#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_fs.h"
#include "svn_xml.h"
#include "svn_path.h"

#include "dav_svn.h"


typedef struct {
  const dav_resource *resource;

  /* the revision we are updating to. used to generated IDs. */
  svn_fs_root_t *rev_root;

  const char *anchor;

  /* if doing a regular update, then dst_path == anchor.  if this is a
     'switch' operation, then this field is the fs path that is being
     switched to.  This path needs to telescope in the update-editor
     just like 'anchor' above; it's used for retrieving CR's and
     vsn-url's during the edit. */
  const char *dst_path;

  /* ### the two fields below will go away, when we switch to filters for
     ### report generation */

  /* pool for storing output text */
  apr_pool_t *opool;

  /* where to place the output */
  apr_text_header *output;
} update_ctx_t;

typedef struct {
  apr_pool_t *pool;
  update_ctx_t *uc;
  const char *path;    /* a telescoping extension of uc->anchor */
  const char *path2;   /* a telescoping extension of uc->dst_path */
  svn_boolean_t added;
  apr_array_header_t *changed_props;
  apr_array_header_t *removed_props;
} item_baton_t;

#define DIR_OR_FILE(is_dir) ((is_dir) ? "directory" : "file")


static item_baton_t *make_child_baton(item_baton_t *parent, const char *name,
				      svn_boolean_t is_dir)
{
  item_baton_t *baton;
  apr_pool_t *pool;

  if (is_dir)
    pool = svn_pool_create(parent->pool);
  else
    pool = parent->pool;

  baton = apr_pcalloc(pool, sizeof(*baton));
  baton->pool = pool;
  baton->uc = parent->uc;

  /* Telescope the path based on uc->anchor.  */
  if (parent->path[1] == '\0')  /* must be "/" */
    baton->path = apr_pstrcat(pool, "/", name, NULL);
  else
    baton->path = apr_pstrcat(pool, parent->path, "/", name, NULL);

  /* Telescope the path based on uc->dst_path in the exact same way. */
  if (parent->path2[1] == '\0')  /* must be "/" */
    baton->path2 = apr_pstrcat(pool, "/", name, NULL);
  else
    baton->path2 = apr_pstrcat(pool, parent->path2, "/", name, NULL);

  return baton;
}

static void send_xml(update_ctx_t *uc, const char *fmt, ...)
{
  va_list ap;
  const char *s;

  va_start(ap, fmt);
  s = apr_pvsprintf(uc->opool, fmt, ap);
  va_end(ap);

  apr_text_append(uc->opool, uc->output, s);
}

static void send_vsn_url(item_baton_t *baton)
{
  svn_error_t *serr;
  svn_fs_id_t *id;
  svn_stringbuf_t *stable_id;
  const char *href;

  /* note: baton->path has a leading "/" */
  serr = svn_fs_node_id(&id, baton->uc->rev_root, baton->path2, baton->pool);
  if (serr != NULL)
    {
      /* ### what to do? */
      return;
    }

  stable_id = svn_fs_unparse_id(id, baton->pool);
  svn_stringbuf_appendcstr(stable_id, baton->path2);

  href = dav_svn_build_uri(baton->uc->resource->info->repos,
			   DAV_SVN_BUILD_URI_VERSION,
			   SVN_INVALID_REVNUM, stable_id->data,
			   0 /* add_href */, baton->pool);

  send_xml(baton->uc, 
           "<D:checked-in><D:href>%s</D:href></D:checked-in>" DEBUG_CR, 
           apr_xml_quote_string (baton->pool, href, 1));
}

static void add_helper(svn_boolean_t is_dir,
		       const char *name,
		       item_baton_t *parent,
		       svn_stringbuf_t *copyfrom_path,
		       svn_revnum_t copyfrom_revision,
		       void **child_baton)
{
  item_baton_t *child;
  const char *qname;

  child = make_child_baton(parent, name, is_dir);
  child->added = TRUE;

  qname = apr_xml_quote_string(child->pool, name, 1);

  if (copyfrom_path == NULL)
    send_xml(child->uc, "<S:add-%s name=\"%s\">" DEBUG_CR,
             DIR_OR_FILE(is_dir), qname);
  else
    {
      const char *qcopy;

      qcopy = apr_xml_quote_string(child->pool, copyfrom_path->data, 1);
      send_xml(child->uc,
	       "<S:add-%s name=\"%s\" "
	       "copyfrom-path=\"%s\" copyfrom-rev=\"%ld\"/>" DEBUG_CR,
               DIR_OR_FILE(is_dir),
	       qname, qcopy, copyfrom_revision);
    }

  send_vsn_url(child);

  *child_baton = child;
}

static void open_helper(svn_boolean_t is_dir,
                        const char *name,
                        item_baton_t *parent,
                        svn_revnum_t base_revision,
                        void **child_baton)
{
  item_baton_t *child;
  const char *qname;

  child = make_child_baton(parent, name, is_dir);

  qname = apr_xml_quote_string(child->pool, name, 1);
  /* ### Sat 24 Nov 2001: leaving this as "replace-" while clients get
     upgraded.  Will change to "open-" soon.  -kff */
  send_xml(child->uc, "<S:replace-%s name=\"%s\" rev=\"%ld\">" DEBUG_CR,
	   DIR_OR_FILE(is_dir), qname, base_revision);

  send_vsn_url(child);

  *child_baton = child;
}

static void close_helper(svn_boolean_t is_dir, item_baton_t *baton)
{
  int i;
  
  /* ### ack!  binary names won't float here! */
  if (baton->removed_props && (! baton->added))
    {
      svn_stringbuf_t *qname;

      for (i = 0; i < baton->removed_props->nelts; i++)
        {
          /* We already XML-escaped the property name in change_xxx_prop. */
          qname = ((svn_stringbuf_t **)(baton->removed_props->elts))[i];
          send_xml(baton->uc, "<S:remove-prop name=\"%s\"/>" DEBUG_CR,
                   qname->data);
        }
    }
  if (baton->changed_props && (! baton->added))
    {
      /* ### for now, we will simply tell the client to fetch all the
         props */
      send_xml(baton->uc, "<S:fetch-props/>" DEBUG_CR);
    }

  /* Unconditionally output the 3 CR-related properties right here.
     ### later on, compress via the 'scattered table' solution as
     discussed with gstein.  -bmcs */
  {
    svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
    svn_string_t *committed_date = NULL;
    svn_string_t *last_author = NULL;
    
    /* Get the CR and two derivative props. ### check for error returns. */
    svn_fs_node_created_rev(&committed_rev,
                            baton->uc->rev_root, baton->path2, baton->pool);
    svn_fs_revision_prop(&committed_date,
                         baton->uc->resource->info->repos->fs,
                         committed_rev, SVN_PROP_REVISION_DATE, baton->pool);
    svn_fs_revision_prop(&last_author,
                         baton->uc->resource->info->repos->fs,
                         committed_rev, SVN_PROP_REVISION_AUTHOR, baton->pool);
    
    /* ### grrr, these DAV: property names are already #defined in
       ra_dav.h, and statically defined in liveprops.c.  And now
       they're hardcoded here.  Isn't there some header file that both
       sides of the network can share?? */
    send_xml(baton->uc, "<S:prop>");
    send_xml(baton->uc, "<D:version-name>%ld</D:version-name>",
             committed_rev);

    if (committed_date)
      send_xml(baton->uc, "<D:creationdate>%s</D:creationdate>",
               committed_date->data);
    else
      send_xml(baton->uc, "<S:remove-prop name=\"creationdate\"/>");

    if (last_author)
      send_xml(baton->uc, "<D:creator-displayname>%s</D:creator-displayname>",
               last_author->data);
    else
      send_xml(baton->uc, "<S:remove-prop name=\"creator-displayname\"/>");

    send_xml(baton->uc, "</S:prop>\n");
  }

  if (baton->added)
    send_xml(baton->uc, "</S:add-%s>" DEBUG_CR, DIR_OR_FILE(is_dir));
  else
    /* ### Sat 24 Nov 2001: leaving this as "replace-" while clients get
       upgraded.  Will change to "open-" soon.  -kff */
    send_xml(baton->uc, "</S:replace-%s>" DEBUG_CR, DIR_OR_FILE(is_dir));
}

static svn_error_t * upd_set_target_revision(void *edit_baton,
					     svn_revnum_t target_revision)
{
  update_ctx_t *uc = edit_baton;

  send_xml(uc,
	   "<S:update-report xmlns:S=\"" SVN_XML_NAMESPACE "\" "
           "xmlns:D=\"DAV:\">" DEBUG_CR
	   "<S:target-revision rev=\"%ld\"/>" DEBUG_CR, target_revision);

  return NULL;
}

static svn_error_t * upd_open_root(void *edit_baton,
                                   svn_revnum_t base_revision,
                                   void **root_baton)
{
  update_ctx_t *uc = edit_baton;
  apr_pool_t *pool = svn_pool_create(uc->resource->pool);
  item_baton_t *b = apr_pcalloc(pool, sizeof(*b));

  /* note that we create a subpool; the root_baton is passed to the
     close_directory callback, where we will destroy the pool. */

  b->uc = uc;
  b->pool = pool;
  b->path = uc->anchor;
  b->path2 = uc->dst_path;

  *root_baton = b;

  /* ### Sat 24 Nov 2001: leaving this as "replace-" while clients get
     upgraded.  Will change to "open-" soon.  -kff */
  send_xml(uc, "<S:replace-directory rev=\"%ld\">" DEBUG_CR, base_revision);
  send_vsn_url(b);

  return NULL;
}

static svn_error_t * upd_delete_entry(svn_stringbuf_t *name,
                                      svn_revnum_t revision,
				      void *parent_baton)
{
  item_baton_t *parent = parent_baton;
  const char *qname;

  qname = apr_xml_quote_string(parent->pool, name->data, 1);
  send_xml(parent->uc, "<S:delete-entry name=\"%s\"/>" DEBUG_CR, qname);

  return NULL;
}

static svn_error_t * upd_add_directory(svn_stringbuf_t *name,
				       void *parent_baton,
				       svn_stringbuf_t *copyfrom_path,
				       svn_revnum_t copyfrom_revision,
				       void **child_baton)
{
  add_helper(TRUE /* is_dir */,
	     name->data, parent_baton, copyfrom_path, copyfrom_revision,
	     child_baton);
  return NULL;
}

static svn_error_t * upd_open_directory(svn_stringbuf_t *name,
                                        void *parent_baton,
                                        svn_revnum_t base_revision,
                                        void **child_baton)
{
  open_helper(TRUE /* is_dir */,
              name->data, parent_baton, base_revision, child_baton);
  return NULL;
}

static svn_error_t * upd_change_xxx_prop(void *baton,
					 svn_stringbuf_t *name,
					 svn_stringbuf_t *value)
{
  item_baton_t *b = baton;
  svn_stringbuf_t *qname;

  qname = svn_stringbuf_create (apr_xml_quote_string (b->pool, name->data, 1),
                                b->pool);
  if (value)
    {
      if (! b->changed_props)
        b->changed_props = apr_array_make (b->pool, 1, sizeof (name));

      (*((svn_stringbuf_t **)(apr_array_push (b->changed_props)))) = qname;
    }
  else
    {
      if (! b->removed_props)
        b->removed_props = apr_array_make (b->pool, 1, sizeof (name));

      (*((svn_stringbuf_t **)(apr_array_push (b->removed_props)))) = qname;
    }
  return NULL;
}

static svn_error_t * upd_close_directory(void *dir_baton)
{
  item_baton_t *dir = dir_baton;

  close_helper(TRUE /* is_dir */, dir);
  svn_pool_destroy(dir->pool);

  return NULL;
}

static svn_error_t * upd_add_file(svn_stringbuf_t *name,
				  void *parent_baton,
				  svn_stringbuf_t *copyfrom_path,
				  svn_revnum_t copyfrom_revision,
				  void **file_baton)
{
  add_helper(FALSE /* is_dir */,
	     name->data, parent_baton, copyfrom_path, copyfrom_revision,
	     file_baton);
  return NULL;
}

static svn_error_t * upd_open_file(svn_stringbuf_t *name,
                                   void *parent_baton,
                                   svn_revnum_t base_revision,
                                   void **file_baton)
{
  open_helper(FALSE /* is_dir */,
              name->data, parent_baton, base_revision, file_baton);
  return NULL;
}

static svn_error_t * noop_handler(svn_txdelta_window_t *window, void *baton)
{
  return NULL;
}

static svn_error_t * upd_apply_textdelta(void *file_baton, 
                                       svn_txdelta_window_handler_t *handler,
                                       void **handler_baton)
{
  item_baton_t *file = file_baton;

  /* if we added the file, then no need to tell the client to fetch it */
  if (!file->added)
    send_xml(file->uc, "<S:fetch-file/>" DEBUG_CR);

  *handler = noop_handler;

  return NULL;
}

static svn_error_t * upd_close_file(void *file_baton)
{
  close_helper(FALSE /* is_dir */, file_baton);
  return NULL;
}

static svn_error_t * upd_close_edit(void *edit_baton)
{
  update_ctx_t *uc = edit_baton;

  send_xml(uc, "</S:update-report>" DEBUG_CR);

  return NULL;
}


dav_error * dav_svn__update_report(const dav_resource *resource,
				   const apr_xml_doc *doc,
				   apr_text_header *report)
{
  svn_delta_edit_fns_t *editor;
  apr_xml_elem *child;
  void *rbaton;
  update_ctx_t uc = { 0 };
  svn_revnum_t revnum = SVN_INVALID_REVNUM;
  int ns;
  svn_error_t *serr;
  const char *dst_path = NULL;
  const char *dir_delta_target = NULL;
  const dav_svn_repos *repos = resource->info->repos;
  const char *target = NULL;
  svn_boolean_t recurse = TRUE;

  if (resource->type != DAV_RESOURCE_TYPE_REGULAR)
    {
      return dav_new_error(resource->pool, HTTP_CONFLICT, 0,
                           "This report can only be run against a "
                           "version-controlled resource.");
    }

  ns = dav_svn_find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    {
      return dav_new_error(resource->pool, HTTP_BAD_REQUEST, 0,
                           "The request does not contain the 'svn:' "
                           "namespace, so it is not going to have an "
                           "svn:target-revision element. That element "
                           "is required.");
    }
  
  for (child = doc->root->first_child; child != NULL; child = child->next)
    {
      if (child->ns == ns && strcmp(child->name, "target-revision") == 0)
        {
          /* ### assume no white space, no child elems, etc */
          revnum = SVN_STR_TO_REV(child->first_cdata.first->text);
        }
      if (child->ns == ns && strcmp(child->name, "dst-path") == 0)
        {
          /* ### assume no white space, no child elems, etc */
          dav_svn_uri_info this_info;

          /* split up the 2nd public URL. */
          serr = dav_svn_simple_parse_uri(&this_info, resource,
                                          child->first_cdata.first->text,
                                          resource->pool);
          if (serr != NULL)
            {
              return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                         "Could not parse dst-path URL.");
            }

          dst_path = apr_pstrdup(resource->pool, this_info.repos_path);
        }

      if (child->ns == ns && strcmp(child->name, "update-target") == 0)
        {
          /* ### assume no white space, no child elems, etc */
          target = child->first_cdata.first->text;
        }
      if (child->ns == ns && strcmp(child->name, "recursive") == 0)
        {
          /* ### assume no white space, no child elems, etc */
          if (strcmp(child->first_cdata.first->text, "no") == 0)
              recurse = FALSE;
        }
    }

  if (revnum == SVN_INVALID_REVNUM)
    {
      serr = svn_fs_youngest_rev(&revnum, repos->fs, resource->pool);
      if (serr != NULL)
        {
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not determine the youngest "
                                     "revision for the update process.");
        }
    }

  /* If dst_path never came over the wire, then assume this is a
     normal update.  */
  if (dst_path == NULL)
    {
      /* All vsn-urls and CR props should be mined from the normal
         anchor of the update:  */
      dst_path = apr_pstrdup(resource->pool, resource->info->repos_path);

      /* The 2nd argument to dir_delta should be [anchor + target]. */
      dir_delta_target = dst_path;
      if (target)
        dir_delta_target = apr_pstrcat(resource->pool, 
                                       dir_delta_target, "/", target, NULL);
    }
  else  /* this is some kind of 'switch' operation */
    {
      /* All vsn-urls and CR props will be mined from dst_path, which
         should already be equal to the fs portion of the extra URL we
         received. */

      /* The 2nd argument to dir_delta should be the fs portion the
         extra URL. */
      dir_delta_target = dst_path;
    }


  editor = svn_delta_old_default_editor(resource->pool);
  editor->set_target_revision = upd_set_target_revision;
  editor->open_root = upd_open_root;
  editor->delete_entry = upd_delete_entry;
  editor->add_directory = upd_add_directory;
  editor->open_directory = upd_open_directory;
  editor->change_dir_prop = upd_change_xxx_prop;
  editor->close_directory = upd_close_directory;
  editor->add_file = upd_add_file;
  editor->open_file = upd_open_file;
  editor->apply_textdelta = upd_apply_textdelta;
  editor->change_file_prop = upd_change_xxx_prop;
  editor->close_file = upd_close_file;
  editor->close_edit = upd_close_edit;

  uc.resource = resource;
  uc.opool = resource->pool;  /* ### not ideal, but temporary anyhow */
  uc.output = report;
  uc.anchor = resource->info->repos_path;
  uc.dst_path = dst_path;

  /* Get the root of the revision we want to update to. This will be used
     to generated stable id values. */
  serr = svn_fs_revision_root(&uc.rev_root, repos->fs, revnum, resource->pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "The revision root could not be created.");
    }

  /* When we call svn_repos_finish_report, it will ultimately run
     dir_delta() between REPOS_PATH/TARGET and TARGET_PATH.  In the
     case of an update or status, these paths should be identical.  In
     the case of a switch, they should be different. */
  serr = svn_repos_begin_report(&rbaton, revnum, repos->username, 
                                repos->repos, 
                                resource->info->repos_path, target,
                                dir_delta_target,
                                FALSE, /* don't send text-deltas */
                                recurse,
                                editor, &uc, resource->pool);

  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "The state report gatherer could not be "
				 "created.");
    }

  /* scan the XML doc for state information */
  for (child = doc->root->first_child; child != NULL; child = child->next)
    if (child->ns == ns)
      {
        if (strcmp(child->name, "entry") == 0)
          {
            svn_revnum_t rev;
            const char *path;

            /* ### assume first/only attribute is the rev */
            rev = SVN_STR_TO_REV(child->attr->value);

            /* get cdata, stipping whitespace */
            path = dav_xml_get_cdata(child, resource->pool, 1);

            serr = svn_repos_set_path(rbaton, path, rev);
            if (serr != NULL)
              {
                /* ### This removes the fs txn.  todo: check error. */
                svn_repos_abort_report(rbaton);
                return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                           "A failure occurred while "
                                           "recording one of the items of "
                                           "working copy state.");
              }
          }
        else if (strcmp(child->name, "missing") == 0)
          {
            const char *path;

            /* get cdata, stipping whitespace */
            path = dav_xml_get_cdata(child, resource->pool, 1);

            serr = svn_repos_delete_path(rbaton, path);
            if (serr != NULL)
              {
                /* ### This removes the fs txn.  todo: check error. */
                svn_repos_abort_report(rbaton);
                return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                           "A failure occurred while "
                                           "recording one of the (missing) "
                                           "items of working copy state.");
              }
          }
      }

  /* this will complete the report, and then drive our editor to generate
     the response to the client. */
  serr = svn_repos_finish_report(rbaton);
  if (serr != NULL)
    {
      /* ### This removes the fs txn.  todo: check error. */
      svn_repos_abort_report(rbaton);
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
				 "A failure occurred during the completion "
				 "and response generation for the update "
				 "report.");
    }

  return NULL;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
