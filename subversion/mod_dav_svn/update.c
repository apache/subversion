/*
 * update.c: handle the update-report request and response
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_xml.h>
#include <mod_dav.h>

#include "svn_repos.h"
#include "svn_fs.h"
#include "svn_xml.h"

#include "dav_svn.h"


typedef struct {
  const dav_resource *resource;

  /* the revision we are updating to. used to generated IDs. */
  svn_fs_root_t *rev_root;

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
  const char *path;
  svn_boolean_t added;
  svn_boolean_t seen_prop_change;
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

  if (parent->path[1] == '\0')  /* must be "/" */
    baton->path = apr_pstrcat(pool, "/", name, NULL);
  else
    baton->path = apr_pstrcat(pool, parent->path, "/", name, NULL);

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
  svn_string_t *stable_id;
  const char *href;

  /* note: baton->path has a leading "/" */
  serr = svn_fs_node_id(&id, baton->uc->rev_root, baton->path, baton->pool);
  if (serr != NULL)
    {
      /* ### what to do? */
      return;
    }

  stable_id = svn_fs_unparse_id(id, baton->pool);
  svn_string_appendcstr(stable_id, baton->path);

  href = dav_svn_build_uri(baton->uc->resource->info->repos,
			   DAV_SVN_BUILD_URI_VERSION,
			   SVN_INVALID_REVNUM, stable_id->data,
			   1 /* add_href */, baton->pool);

  send_xml(baton->uc, "<D:checked-in>%s</D:checked-in>" DEBUG_CR, href);
}

static void add_helper(svn_boolean_t is_dir,
		       const char *name,
		       item_baton_t *parent,
		       svn_string_t *copyfrom_path,
		       svn_revnum_t copyfrom_revision,
		       void **child_baton)
{
  item_baton_t *child;
  const char *qname;

  child = make_child_baton(parent, name, is_dir);
  child->added = TRUE;

  qname = apr_xml_quote_string(child->pool, name, 1);

  if (copyfrom_path == NULL)
    send_xml(child->uc, "<S:add-%s name=\"%s\"/>" DEBUG_CR,
             DIR_OR_FILE(is_dir), qname);
  else
    {
      const char *qcopy;

      qcopy = apr_xml_quote_string(child->pool, copyfrom_path->data, 1);
      send_xml(child->uc,
	       "<S:add-%s name=\"%s\" "
	       "copyfrom-path=\"%s\" copyfrom-rev=\"%ld\"/>" DEBUG_CR,
               DIR_OR_FILE(is_dir),
	       qname, copyfrom_path->data, copyfrom_revision);
    }

  send_vsn_url(child);

  *child_baton = child;
}

static void replace_helper(svn_boolean_t is_dir,
			   const char *name,
			   item_baton_t *parent,
			   svn_revnum_t base_revision,
			   void **child_baton)
{
  item_baton_t *child;
  const char *qname;

  child = make_child_baton(parent, name, is_dir);

  qname = apr_xml_quote_string(child->pool, name, 1);
  send_xml(child->uc, "<S:replace-%s name=\"%s\" rev=\"%ld\">" DEBUG_CR,
	   DIR_OR_FILE(is_dir), qname, base_revision);

  send_vsn_url(child);

  *child_baton = child;
}

static void close_helper(svn_boolean_t is_dir, item_baton_t *baton)
{
  if (baton->seen_prop_change)
    send_xml(baton->uc, "<S:fetch-props/>" DEBUG_CR);

  if (baton->added)
    send_xml(baton->uc, "</S:add-%s>" DEBUG_CR, DIR_OR_FILE(is_dir));
  else
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

static svn_error_t * upd_replace_root(void *edit_baton,
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
  b->path = "/";

  *root_baton = b;

  send_xml(uc, "<S:replace-directory rev=\"%ld\">" DEBUG_CR, base_revision);

  return NULL;
}

static svn_error_t * upd_delete_entry(svn_string_t *name,
				      void *parent_baton)
{
  item_baton_t *parent = parent_baton;
  const char *qname;

  qname = apr_xml_quote_string(parent->pool, name->data, 1);
  send_xml(parent->uc, "<S:delete-entry name=\"%s\"/>" DEBUG_CR, name->data);

  return NULL;
}

static svn_error_t * upd_add_directory(svn_string_t *name,
				       void *parent_baton,
				       svn_string_t *copyfrom_path,
				       svn_revnum_t copyfrom_revision,
				       void **child_baton)
{
  add_helper(TRUE /* is_dir */,
	     name->data, parent_baton, copyfrom_path, copyfrom_revision,
	     child_baton);
  return NULL;
}

static svn_error_t * upd_replace_directory(svn_string_t *name,
					   void *parent_baton,
					   svn_revnum_t base_revision,
					   void **child_baton)
{
  replace_helper(TRUE /* is_dir */,
		 name->data, parent_baton, base_revision, child_baton);
  return NULL;
}

static svn_error_t * upd_change_xxx_prop(void *baton,
					 svn_string_t *name,
					 svn_string_t *value)
{
  item_baton_t *b = baton;

  b->seen_prop_change = TRUE;

  return NULL;
}

static svn_error_t * upd_close_directory(void *dir_baton)
{
  item_baton_t *dir = dir_baton;

  close_helper(TRUE /* is_dir */, dir);
  svn_pool_destroy(dir->pool);

  return NULL;
}

static svn_error_t * upd_add_file(svn_string_t *name,
				  void *parent_baton,
				  svn_string_t *copyfrom_path,
				  svn_revnum_t copyfrom_revision,
				  void **file_baton)
{
  add_helper(FALSE /* is_dir */,
	     name->data, parent_baton, copyfrom_path, copyfrom_revision,
	     file_baton);
  return NULL;
}

static svn_error_t * upd_replace_file(svn_string_t *name,
				      void *parent_baton,
				      svn_revnum_t base_revision,
				      void **file_baton)
{
  replace_helper(FALSE /* is_dir */,
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
  svn_string_t *fs_base;
  svn_revnum_t revnum = SVN_INVALID_REVNUM;
  int ns;
  svn_error_t *serr;
  svn_string_t *pathstr;

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
                           "The request does not contain the 'SVN:' "
                           "namespace, so it is not going to have an "
                           "SVN:target-revision element. That element "
                           "is required.");
    }
  
  for (child = doc->root->first_child; child != NULL; child = child->next)
    if (child->ns == ns && strcmp(child->name, "target-revision") == 0)
      {
	/* ### assume no white space, no child elems, etc */
	revnum = atol(child->first_cdata.first->text);
	break;
      }
  if (revnum == SVN_INVALID_REVNUM)
    {
      serr = svn_fs_youngest_rev(&revnum, resource->info->repos->fs,
                                 resource->pool);
      if (serr != NULL)
        {
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not determine the youngest "
                                     "revision for the update process.");
        }
    }

  editor = svn_delta_default_editor(resource->pool);
  editor->set_target_revision = upd_set_target_revision;
  editor->replace_root = upd_replace_root;
  editor->delete_entry = upd_delete_entry;
  editor->add_directory = upd_add_directory;
  editor->replace_directory = upd_replace_directory;
  editor->change_dir_prop = upd_change_xxx_prop;
  editor->close_directory = upd_close_directory;
  editor->add_file = upd_add_file;
  editor->replace_file = upd_replace_file;
  editor->apply_textdelta = upd_apply_textdelta;
  editor->change_file_prop = upd_change_xxx_prop;
  editor->close_file = upd_close_file;
  editor->close_edit = upd_close_edit;

  uc.resource = resource;
  uc.opool = resource->pool;  /* ### not ideal, but temporary anyhow */
  uc.output = report;

  /* Get the root of the revision we want to update to. This will be used
     to generated stable id values. */
  serr = svn_fs_revision_root(&uc.rev_root, resource->info->repos->fs,
			      revnum, resource->pool);
  if (serr != NULL)
    {
    }

  fs_base = svn_string_create(resource->info->repos_path, resource->pool);
  serr = svn_repos_begin_report(&rbaton, revnum,
				resource->info->repos->fs, fs_base,
				editor, &uc, resource->pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "The state report gatherer could not be "
				 "created.");
    }

  /* ### move this into svn_string.h */
#define MAKE_BUFFER(p) svn_string_ncreate("", 0, (p))
  pathstr = MAKE_BUFFER(resource->pool);

  /* scan the XML doc for state information */
  for (child = doc->root->first_child; child != NULL; child = child->next)
    if (child->ns == ns && strcmp(child->name, "entry") == 0)
      {
	svn_revnum_t rev;
	const char *path;

	/* ### assume first/only attribute is the rev */
	rev = atol(child->attr->value);

	path = dav_xml_get_cdata(child, resource->pool, 1 /* strip_white */);

	svn_string_set(pathstr, path);
	serr = svn_repos_set_path(rbaton, pathstr, rev);
	if (serr != NULL)
	  {
	    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
				       "A failure occurred while recording "
				       "one of the items of working copy "
				       "state.");
	  }
      }

  /* this will complete the report, and then drive our editor to generate
     the response to the client. */
  serr = svn_repos_finish_report(rbaton);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
				 "A failure occurred during the completion "
				 "and response generation for the update "
				 "report.");
    }

  return NULL;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
