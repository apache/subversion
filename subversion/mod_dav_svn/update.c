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

  /* this buffers the output for a bit and is automatically flushed,
     at appropriate times, by the Apache filter system. */
  apr_bucket_brigade *bb;

  /* where to deliver the output */
  ap_filter_t *output;

  /* where do these editor paths *really* point to? */
  apr_hash_t *pathmap;

  /* are we doing a resource walk? */
  svn_boolean_t resource_walk;

} update_ctx_t;

typedef struct {
  apr_pool_t *pool;
  update_ctx_t *uc;
  const char *name;    /* the single-component name of this item */
  const char *path;    /* a telescoping extension of uc->anchor */
  const char *path2;   /* a telescoping extension of uc->dst_path */
  const char *path3;   /* a telescoping extension of uc->dst_path
                            without dst_path as prefix. */
  svn_boolean_t added;
  apr_array_header_t *changed_props; /* array of const char * prop names */
  apr_array_header_t *removed_props; /* array of const char * prop names */

  /* "entry props" */
  const char *committed_rev;
  const char *committed_date;
  const char *last_author;

} item_baton_t;


#define DIR_OR_FILE(is_dir) ((is_dir) ? "directory" : "file")


/* add PATH to the pathmap HASH with a repository path of LINKPATH.
   if LINKPATH is NULL, PATH will map to itself. */
static void add_to_path_map(apr_hash_t *hash,
                            const char *path,
                            const char *linkpath)
{
  /* normalize 'root paths' to have a slash */
  const char *norm_path = strcmp(path, "") ? path : "/";

  /* if there is an actual linkpath given, it is the repos path, else
     our path maps to itself. */
  const char *repos_path = linkpath ? linkpath : norm_path;

  /* now, geez, put the path in the map already! */
  apr_hash_set(hash, path, APR_HASH_KEY_STRING, (void *)repos_path);
}


/* return the actual repository path referred to by the editor's PATH,
   allocated in POOL, determined by examining the pathmap HASH. */
static const char *get_from_path_map(apr_hash_t *hash,
                                     const char *path,
                                     apr_pool_t *pool)
{
  const char *repos_path;
  svn_stringbuf_t *my_path;
  
  /* no hash means no map.  that's easy enough. */
  if (! hash)
    return apr_pstrdup(pool, path);
  
  if ((repos_path = apr_hash_get(hash, path, APR_HASH_KEY_STRING)))
    {
      /* what luck!  this path is a hash key!  if there is a linkpath,
         use that, else return the path itself. */
      return apr_pstrdup(pool, repos_path);
    }

  /* bummer.  PATH wasn't a key in path map, so we get to start
     hacking off components and looking for a parent from which to
     derive a repos_path.  use a stringbuf for convenience. */
  my_path = svn_stringbuf_create(path, pool);
  do 
    {
      svn_path_remove_component(my_path);
      if ((repos_path = apr_hash_get(hash, my_path->data, my_path->len)))
        {
          /* we found a mapping ... but of one of PATH's parents.
             soooo, we get to re-append the chunks of PATH that we
             broke off to the REPOS_PATH we found. */
          return apr_pstrcat(pool, repos_path, "/", 
                             path + my_path->len + 1, NULL);
        }
    }
  while (! svn_path_is_empty(my_path) && strcmp (my_path->data, "/") != 0);
  
  /* well, we simply never found anything worth mentioning the map.
     PATH is its own default finding, then. */
  return apr_pstrdup(pool, path);
}

static item_baton_t *make_child_baton(item_baton_t *parent, 
                                      const char *path,
                                      apr_pool_t *pool)
{
  item_baton_t *baton;

  baton = apr_pcalloc(pool, sizeof(*baton));
  baton->pool = pool;
  baton->uc = parent->uc;
  baton->name = svn_path_basename(path, pool);

  /* Telescope the path based on uc->anchor.  */
  baton->path = svn_path_join(parent->path, baton->name, pool);

  /* Telescope the path based on uc->dst_path in the exact same way. */
  baton->path2 = svn_path_join(parent->path2, baton->name, pool);

  /* Telescope the third path:  it's relative, not absolute, to dst_path. */
  baton->path3 = svn_path_join(parent->path3, baton->name, pool);

  return baton;
}

static void send_xml(update_ctx_t *uc, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  (void) apr_brigade_vprintf(uc->bb, ap_filter_flush, uc->output, fmt, ap);
  va_end(ap);
}

static void send_vsn_url(item_baton_t *baton, apr_pool_t *pool)
{
  const char *href;
  const char *path;
  svn_revnum_t revision;

  /* when sending back vsn urls, we'll try to see what this editor
     path really points to in the repos.  if it doesn't point to
     something other than itself, we'll use path2.  if it does point
     to something else, we'll use the path that it points to. */
  path = get_from_path_map(baton->uc->pathmap, baton->path, pool);
  path = strcmp(path, baton->path) ? path : baton->path2;

  /* Try to use the CR, assuming the path exists in CR. */
  revision = dav_svn_get_safe_cr(baton->uc->rev_root, path, pool);
    
  href = dav_svn_build_uri(baton->uc->resource->info->repos,
			   DAV_SVN_BUILD_URI_VERSION,
			   revision, path, 0 /* add_href */, pool);

  send_xml(baton->uc, 
           "<D:checked-in><D:href>%s</D:href></D:checked-in>" DEBUG_CR, 
           apr_xml_quote_string (pool, href, 1));
}

static void add_helper(svn_boolean_t is_dir,
		       const char *path,
		       item_baton_t *parent,
		       const char *copyfrom_path,
		       svn_revnum_t copyfrom_revision,
                       apr_pool_t *pool,
		       void **child_baton)
{
  item_baton_t *child;
  const char *qname;
  const char *qpath;
  update_ctx_t *uc = parent->uc;

  child = make_child_baton(parent, path, pool);
  child->added = TRUE;

  if (uc->resource_walk)
    {
      qpath = apr_xml_quote_string(pool, child->path3, 1);

      send_xml(child->uc, "<S:resource path=\"%s\">" DEBUG_CR, qpath);      
    }
  else
    {
      qname = apr_xml_quote_string(pool, child->name, 1);
      
      if (copyfrom_path == NULL)
        send_xml(child->uc, "<S:add-%s name=\"%s\">" DEBUG_CR,
                 DIR_OR_FILE(is_dir), qname);
      else
        {
          const char *qcopy;
          
          qcopy = apr_xml_quote_string(pool, copyfrom_path, 1);
          send_xml(child->uc,
                   "<S:add-%s name=\"%s\" "
                   "copyfrom-path=\"%s\" copyfrom-rev=\"%"
                   SVN_REVNUM_T_FMT "\"/>" DEBUG_CR,
                   DIR_OR_FILE(is_dir),
                   qname, qcopy, copyfrom_revision);
        }
    }

  send_vsn_url(child, pool);

  if (uc->resource_walk)
    send_xml(child->uc, "</S:resource>" DEBUG_CR);

  *child_baton = child;
}

static void open_helper(svn_boolean_t is_dir,
                        const char *path,
                        item_baton_t *parent,
                        svn_revnum_t base_revision,
                        apr_pool_t *pool,
                        void **child_baton)
{
  item_baton_t *child;
  const char *qname;

  child = make_child_baton(parent, path, pool);

  qname = apr_xml_quote_string(pool, child->name, 1);

  send_xml(child->uc, "<S:open-%s name=\"%s\" rev=\"%"
           SVN_REVNUM_T_FMT "\">" DEBUG_CR,
	   DIR_OR_FILE(is_dir), qname, base_revision);

  send_vsn_url(child, pool);

  *child_baton = child;
}

static void close_helper(svn_boolean_t is_dir, item_baton_t *baton)
{
  int i;
  
  if (baton->uc->resource_walk)
    return;

  /* ### ack!  binary names won't float here! */
  if (baton->removed_props && (! baton->added))
    {
      const char *qname;

      for (i = 0; i < baton->removed_props->nelts; i++)
        {
          /* We already XML-escaped the property name in change_xxx_prop. */
          qname = ((const char **)(baton->removed_props->elts))[i];
          send_xml(baton->uc, "<S:remove-prop name=\"%s\"/>" DEBUG_CR, qname);
        }
    }
  if (baton->changed_props && (! baton->added))
    {
      /* ### for now, we will simply tell the client to fetch all the
         props */
      send_xml(baton->uc, "<S:fetch-props/>" DEBUG_CR);
    }

  /* Output the 3 CR-related properties right here.
     ### later on, compress via the 'scattered table' solution as
     discussed with gstein.  -bmcs */
  {
    /* ### grrr, these DAV: property names are already #defined in
       ra_dav.h, and statically defined in liveprops.c.  And now
       they're hardcoded here.  Isn't there some header file that both
       sides of the network can share?? */
    
    send_xml(baton->uc, "<S:prop>");
    
    /* ### special knowledge: svn_repos_dir_delta will never send
     *removals* of the commit-info "entry props". */
    if (baton->committed_rev)
      send_xml(baton->uc, "<D:version-name>%s</D:version-name>",
               baton->committed_rev);
    
    if (baton->committed_date)
      send_xml(baton->uc, "<D:creationdate>%s</D:creationdate>",
               baton->committed_date);
    
    if (baton->last_author)
      send_xml(baton->uc, "<D:creator-displayname>%s</D:creator-displayname>",
               baton->last_author);
    
    send_xml(baton->uc, "</S:prop>\n");
  }
    
  if (baton->added)
    send_xml(baton->uc, "</S:add-%s>" DEBUG_CR, DIR_OR_FILE(is_dir));
  else
    send_xml(baton->uc, "</S:open-%s>" DEBUG_CR, DIR_OR_FILE(is_dir));
}

static svn_error_t * upd_set_target_revision(void *edit_baton,
					     svn_revnum_t target_revision,
                                             apr_pool_t *pool)
{
  update_ctx_t *uc = edit_baton;

  if (! uc->resource_walk)
    send_xml(uc,
             DAV_XML_HEADER DEBUG_CR
             "<S:update-report xmlns:S=\"" SVN_XML_NAMESPACE "\" "
             "xmlns:D=\"DAV:\">" DEBUG_CR
             "<S:target-revision rev=\"%" SVN_REVNUM_T_FMT "\"/>" DEBUG_CR,
             target_revision);

  return SVN_NO_ERROR;
}

static svn_error_t * upd_open_root(void *edit_baton,
                                   svn_revnum_t base_revision,
                                   apr_pool_t *pool,
                                   void **root_baton)
{
  update_ctx_t *uc = edit_baton;
  item_baton_t *b = apr_pcalloc(pool, sizeof(*b));

  /* note that we create a subpool; the root_baton is passed to the
     close_directory callback, where we will destroy the pool. */

  b->uc = uc;
  b->pool = pool;
  b->path = uc->anchor;
  b->path2 = uc->dst_path;
  b->path3 = "";

  *root_baton = b;

  if (uc->resource_walk)
    {
      const char *qpath = apr_xml_quote_string(pool, b->path3, 1);
      send_xml(uc, "<S:resource path=\"%s\">" DEBUG_CR, qpath);
    }
  else    
    send_xml(uc, "<S:open-directory rev=\"%" SVN_REVNUM_T_FMT "\">"
             DEBUG_CR, base_revision);

  send_vsn_url(b, pool);

  if (uc->resource_walk)
    send_xml(uc, "</S:resource>" DEBUG_CR);

  return SVN_NO_ERROR;
}

static svn_error_t * upd_delete_entry(const char *path,
                                      svn_revnum_t revision,
				      void *parent_baton,
                                      apr_pool_t *pool)
{
  item_baton_t *parent = parent_baton;
  const char *qname, *name = svn_path_basename(path, pool);
  
  qname = apr_xml_quote_string(pool, name, 1);
  send_xml(parent->uc, "<S:delete-entry name=\"%s\"/>" DEBUG_CR, qname);

  return SVN_NO_ERROR;
}

static svn_error_t * upd_add_directory(const char *path,
				       void *parent_baton,
				       const char *copyfrom_path,
				       svn_revnum_t copyfrom_revision,
                                       apr_pool_t *pool,
				       void **child_baton)
{
  add_helper(TRUE /* is_dir */,
             path, parent_baton, copyfrom_path, copyfrom_revision, pool,
             child_baton);
  return SVN_NO_ERROR;
}

static svn_error_t * upd_open_directory(const char *path,
                                        void *parent_baton,
                                        svn_revnum_t base_revision,
                                        apr_pool_t *pool,
                                        void **child_baton)
{
  open_helper(TRUE /* is_dir */,
              path, parent_baton, base_revision, pool, child_baton);
  return SVN_NO_ERROR;
}

static svn_error_t * upd_change_xxx_prop(void *baton,
					 const char *name,
					 const svn_string_t *value,
                                         apr_pool_t *pool)
{
  item_baton_t *b = baton;
  const char *qname;

  /* For now, specially handle entry props that come through (using
     the ones we care about, discarding the rest).  ### this should go
     away and we should just tunnel those props on through for the
     client to deal with. */
#define NSLEN (sizeof(SVN_PROP_ENTRY_PREFIX) - 1)
  if (! strncmp(name, SVN_PROP_ENTRY_PREFIX, NSLEN))
    {
      if (! strcmp(name, SVN_PROP_ENTRY_COMMITTED_REV))
        b->committed_rev = value ? apr_pstrdup(b->pool, value->data) : NULL;
      else if (! strcmp(name, SVN_PROP_ENTRY_COMMITTED_DATE))
        b->committed_date = value ? apr_pstrdup(b->pool, value->data) : NULL;
      else if (! strcmp(name, SVN_PROP_ENTRY_LAST_AUTHOR))
        b->last_author = value ? apr_pstrdup(b->pool, value->data) : NULL;
      
      return SVN_NO_ERROR;
    }
#undef NSLEN
                
  qname = apr_xml_quote_string (b->pool, name, 1);
  /* apr_xml_quote_string doesn't realloc if there is nothing to
     quote, so dup the name, but only if necessary. */
  if (qname == name)
    qname = apr_pstrdup (b->pool, name);
  if (value)
    {
      if (! b->changed_props)
        b->changed_props = apr_array_make (b->pool, 1, sizeof (name));

      (*((const char **)(apr_array_push (b->changed_props)))) = qname;
    }
  else
    {
      if (! b->removed_props)
        b->removed_props = apr_array_make (b->pool, 1, sizeof (name));

      (*((const char **)(apr_array_push (b->removed_props)))) = qname;
    }
  return SVN_NO_ERROR;
}

static svn_error_t * upd_close_directory(void *dir_baton,
                                         apr_pool_t *pool)
{
  item_baton_t *dir = dir_baton;

  close_helper(TRUE /* is_dir */, dir);

  return SVN_NO_ERROR;
}

static svn_error_t * upd_add_file(const char *path,
				  void *parent_baton,
				  const char *copyfrom_path,
				  svn_revnum_t copyfrom_revision,
                                  apr_pool_t *pool,
				  void **file_baton)
{
  add_helper(FALSE /* is_dir */,
	     path, parent_baton, copyfrom_path, copyfrom_revision, pool,
	     file_baton);
  return SVN_NO_ERROR;
}

static svn_error_t * upd_open_file(const char *path,
                                   void *parent_baton,
                                   svn_revnum_t base_revision,
                                   apr_pool_t *pool,
                                   void **file_baton)
{
  open_helper(FALSE /* is_dir */,
              path, parent_baton, base_revision, pool, file_baton);
  return SVN_NO_ERROR;
}

static svn_error_t * upd_apply_textdelta(void *file_baton, 
                                         apr_pool_t *pool,
                                         svn_txdelta_window_handler_t *handler,
                                         void **handler_baton)
{
  item_baton_t *file = file_baton;

  /* if we added the file, then no need to tell the client to fetch it */
  if (!file->added)
    send_xml(file->uc, "<S:fetch-file/>" DEBUG_CR);

  *handler = NULL;

  return SVN_NO_ERROR;
}

static svn_error_t * upd_close_file(void *file_baton,
                                    apr_pool_t *pool)
{
  close_helper(FALSE /* is_dir */, file_baton);
  return SVN_NO_ERROR;
}


dav_error * dav_svn__update_report(const dav_resource *resource,
				   const apr_xml_doc *doc,
				   ap_filter_t *output)
{
  svn_delta_editor_t *editor;
  apr_xml_elem *child;
  void *rbaton;
  update_ctx_t uc = { 0 };
  svn_revnum_t revnum = SVN_INVALID_REVNUM;
  int ns;
  svn_error_t *serr;
  const char *dst_path = NULL;
  const dav_svn_repos *repos = resource->info->repos;
  const char *target = NULL;
  svn_boolean_t recurse = TRUE, resource_walk = FALSE;

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
      if (child->ns == ns && strcmp(child->name, "resource-walk") == 0)
        {
          /* ### assume no white space, no child elems, etc */
          if (strcmp(child->first_cdata.first->text, "no") != 0)
            resource_walk = TRUE;
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

  editor = svn_delta_default_editor(resource->pool);
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

  uc.resource = resource;
  uc.output = output;  
  uc.anchor = resource->info->repos_path;
  if (dst_path) /* we're doing a 'switch' */
    {      
      if (target) /* if the src is split into anchor/target, so must
                     the telescoping dst_path be. */
        uc.dst_path = svn_path_remove_component_nts(dst_path, resource->pool);
      else
        uc.dst_path = dst_path;
    }
  else  /* we're doing an update, so src and dst are the same. */
    uc.dst_path = uc.anchor;

  uc.bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);
  uc.pathmap = NULL;

  /* Get the root of the revision we want to update to. This will be used
     to generated stable id values. */
  if ((serr = svn_fs_revision_root(&uc.rev_root, repos->fs, 
                                   revnum, resource->pool)))
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "The revision root could not be created.");
    }

  /* When we call svn_repos_finish_report, it will ultimately run
     dir_delta() between REPOS_PATH/TARGET and TARGET_PATH.  In the
     case of an update or status, these paths should be identical.  In
     the case of a switch, they should be different. */
  if ((serr = svn_repos_begin_report(&rbaton, revnum, repos->username, 
                                     repos->repos, 
                                     resource->info->repos_path, target,
                                     dst_path,
                                     FALSE, /* don't send text-deltas */
                                     recurse,
                                     editor, &uc, resource->pool)))
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
            const char *path;
            svn_revnum_t rev = SVN_INVALID_REVNUM;
            const char *linkpath = NULL;
            apr_xml_attr *this_attr = child->attr;

            while (this_attr)
              {
                if (! strcmp(this_attr->name, "rev"))
                  rev = SVN_STR_TO_REV(this_attr->value);
                else if (! strcmp(this_attr->name, "linkpath"))
                  linkpath = this_attr->value;

                this_attr = this_attr->next;
              }
            
            /* we require the `rev' attribute for this to make sense */
            if (! SVN_IS_VALID_REVNUM (rev))
              {
                /* ### This removes the fs txn.  todo: check error. */
                svn_repos_abort_report(rbaton);
                serr = svn_error_create (SVN_ERR_XML_ATTRIB_NOT_FOUND, 0, 
                                         NULL, "rev");
                return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                           "A failure occurred while "
                                           "recording one of the items of "
                                           "working copy state.");
              }

            /* get cdata, stipping whitespace */
            path = dav_xml_get_cdata(child, resource->pool, 1);
            
            if (! linkpath)
              serr = svn_repos_set_path(rbaton, path, rev);
            else
              serr = svn_repos_link_path(rbaton, path, linkpath, rev);
            if (serr != NULL)
              {
                /* ### This removes the fs txn.  todo: check error. */
                svn_repos_abort_report(rbaton);
                return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                           "A failure occurred while "
                                           "recording one of the items of "
                                           "working copy state.");
              }

            /* now, add this path to our path map, but only if we are
               doing a regular update (not a `switch') */
            if (linkpath && (! dst_path))
              {
                const char *this_path
                  = svn_path_join_many(resource->pool,
                                       resource->info->repos_path,
                                       target ? target : path,
                                       target ? path : NULL,
                                       NULL);
                if (! uc.pathmap)
                  uc.pathmap = apr_hash_make(resource->pool);
                add_to_path_map(uc.pathmap, this_path, linkpath);
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

  /* The potential "resource walk" part of the update-report. */
  if (dst_path && resource_walk)  /* this was a 'switch' operation */
    {
      /* Sanity check: if we switched a file, we can't do a resource
         walk.  dir_delta would choke if we pass a filepath as the
         'target'.  Also, there's no need to do the walk, since the
         new vsn-rsc-url was already in the earlier part of the report. */
      svn_node_kind_t dst_kind = svn_fs_check_path(uc.rev_root, dst_path,
                                                   resource->pool);
      if (dst_kind == svn_node_dir)
        {
          /* send a second embedded <S:resource-walk> tree that contains
             the new vsn-rsc-urls for the switched dir.  this walk
             contains essentially nothing but <add> tags. */
          svn_fs_root_t *zero_root;
          svn_fs_revision_root(&zero_root, repos->fs, 0, resource->pool);

          send_xml(&uc, "<S:resource-walk>" DEBUG_CR);

          uc.resource_walk = TRUE;

          /* Compare subtree DST_PATH within a pristine revision to
             revision 0.  This should result in nothing but 'add' calls
             to the editor. */
          serr = svn_repos_dir_delta(/* source is revision 0: */
                                     zero_root, "", NULL,
                                     /* target is 'switch' location: */
                                     uc.rev_root, dst_path,
                                     /* re-use the editor */
                                     editor, &uc,
                                     FALSE, /* no text deltas */
                                     recurse,
                                     TRUE, /* send entryprops */
                                     FALSE, /* no copy history */
                                     resource->pool);

          send_xml(&uc, "</S:resource-walk>" DEBUG_CR);
        }
    }

  /* Now close the report body completely. */
  send_xml(&uc, "</S:update-report>" DEBUG_CR);

  /* flush the contents of the brigade */
  ap_fflush(output, uc.bb);

  /* if an error was produced EITHER by the dir_delta drive or the
     resource-walker... */
  if (serr != NULL)
    {
      /* ### This removes the fs txn.  todo: check error. */
      svn_repos_abort_report(rbaton);
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
				 "A failure occurred during the completion "
				 "and response generation for the update "
				 "report.");
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
