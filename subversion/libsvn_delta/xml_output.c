/*
 * xml_output.c:  output a Subversion "tree-delta" XML stream
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_io.h"
#include "svn_xml.h"
#include "svn_base64.h"
#include "svn_quoprint.h"
#include "apr_pools.h"


/* TODO:

        - Produce real vcdiff data when we have Branko's text delta ->
          vcdiff routines.
	- Do consistency checking on the order of calls, maybe.
	  (Right now we'll just spit out invalid output if the calls
	  come in an incorrect order.)
	- Indentation?  Not really a priority.
*/




/* The types of some of the elements we output.  The actual range of
   valid values is always narrower than the full set, but they
   overlap, so it doesn't quite make sense to have a separate
   enueration for each use.  */
enum elemtype {
  elem_delta_pkg,
  elem_add,
  elem_replace,
  elem_dir,
  elem_dir_prop_delta,
  elem_tree_delta,
  elem_file,
  elem_file_prop_delta
};

struct edit_context
{
  svn_stream_t *output;
  enum elemtype elem;           /* Current element we are inside at
                                   the end of a call.  One of
                                   elem_dir, elem_dir_prop_delta,
                                   elem_tree_delta, elem_file, or
                                   elem_file_prop_delta.  */
  struct file_baton *curfile;
  apr_pool_t *pool;
  int txdelta_id_counter;
  int open_file_count;
  svn_boolean_t root_dir_closed;
};


struct dir_baton
{
  struct edit_context *edit_context;
  enum elemtype addreplace;     /* elem_add or elem_replace, or
                                   elem_delta_pkg for the root
                                   directory.  */
  apr_pool_t *pool;
};


struct file_baton
{
  struct edit_context *edit_context;
  enum elemtype addreplace;
  int txdelta_id;               /* ID of deferred text delta;
                                   0 means we're still working on the file,
                                   -1 means we already saw a text delta.  */
  int closed;			/* 1 if we closed the element already.  */
  apr_pool_t *pool;
};


static struct dir_baton *
make_dir_baton (struct edit_context *ec, enum elemtype addreplace)
{
  apr_pool_t *subpool = svn_pool_create (ec->pool);
  struct dir_baton *db = apr_palloc (subpool, sizeof (*db));

  db->edit_context = ec;
  db->addreplace = addreplace;
  db->pool = subpool;
  return db;
}


static struct file_baton *
make_file_baton (struct edit_context *ec, enum elemtype addreplace)
{
  apr_pool_t *subpool = svn_pool_create (ec->pool);
  struct file_baton *fb = apr_palloc (subpool, sizeof (*fb));

  fb->edit_context = ec;
  fb->addreplace = addreplace;
  fb->txdelta_id = 0;
  fb->closed = 0;
  fb->pool = subpool;
  return fb;
}


static svn_error_t *
close_edit (struct edit_context *ec)
{
  svn_string_t *str = NULL;
  apr_size_t len;

  svn_xml_make_close_tag (&str, ec->pool, "delta-pkg");
  len = str->len;
  SVN_ERR (svn_stream_write (ec->output, str->data, &len));
  SVN_ERR (svn_stream_close (ec->output));
  return SVN_NO_ERROR;
}


/* The meshing between the edit_fns interface and the XML delta format
   is such that we can't usually output the end of an element until we
   go on to the next thing, and for a given call we may or may not
   have already output the beginning of the element we're working on.
   This function takes care of "unwinding" and "winding" from the
   current element to the kind of element we need to work on next.  We
   never have to unwind past a dir element, so the unwinding steps are
   bounded in number and easy to visualize.  The nesting of the
   elements we care about looks like:
  
        dir -> prop_delta
            -> tree_delta -> add/replace -> file -> prop_delta

   We cannot be in an add/replace element at the end of a call, so
   add/replace and file are treated as a unit by this function.  Note
   that although there is no replace or dir element corresponding to
   the root directory (the root directory's tree-delta and/or
   prop-delta elements live directly inside the delta-pkg element), we
   pretend that there is for the sake of regularity.

   This function will "unwind" arbitrarily within that little tree,
   but will only "wind" from dir to tree_delta or prop_delta or from
   file to prop_delta.  Winding through add/replace/file would require
   extra information.

   ELEM specifies the element type we want to get to, with prop_delta
   split out into elem_dir_prop_delta and elem_file_prop_delta
   depending on where the prop_delta is in the little tree.  The
   element type we are currently in is recorded inside EC.  */

static svn_string_t *
get_to_elem (struct edit_context *ec, enum elemtype elem, apr_pool_t *pool)
{
  svn_string_t *str = svn_string_create ("", pool);
  struct file_baton *fb;

  /* Unwind.  Start from the leaves and go back as far as necessary.  */
  if (ec->elem == elem_file_prop_delta && elem != elem_file_prop_delta)
    {
      svn_xml_make_close_tag (&str, pool, "prop-delta");
      ec->elem = elem_file;
    }
  if (ec->elem == elem_file && elem != elem_file
      && elem != elem_file_prop_delta)
    {
      const char *outertag;

      fb = ec->curfile;
      if (fb->txdelta_id == 0)
        {
          char buf[128];
          svn_string_t *idstr;

          /* Leak a little memory from pool to create idstr; all of our
             callers are using temporary pools anyway.  */
          fb->txdelta_id = ec->txdelta_id_counter++;
          sprintf (buf, "%d", fb->txdelta_id);
          idstr = svn_string_create (buf, pool);
          svn_xml_make_open_tag (&str, pool, svn_xml_self_closing,
                                 "text-delta-ref", "id", idstr, NULL);
        }
      svn_xml_make_close_tag (&str, pool, "file");
      outertag = (fb->addreplace == elem_add) ? "add" : "replace";
      svn_xml_make_close_tag (&str, pool, outertag);
      fb->closed = 1;
      ec->curfile = NULL;
      ec->elem = elem_tree_delta;
    }
  if (ec->elem == elem_tree_delta
      && (elem == elem_dir || elem == elem_dir_prop_delta))
    {
      svn_xml_make_close_tag (&str, pool, "tree-delta");
      ec->elem = elem_dir;
    }
  if (ec->elem == elem_dir_prop_delta && elem != elem_dir_prop_delta)
    {
      svn_xml_make_close_tag (&str, pool, "prop-delta");
      ec->elem = elem_dir;
    }

  /* Now wind.  */
  if (ec->elem == elem_dir && elem == elem_tree_delta)
    {
      svn_xml_make_open_tag (&str, pool, svn_xml_normal, "tree-delta", NULL);
      ec->elem = elem_tree_delta;
    }
  if ((ec->elem == elem_dir && elem == elem_dir_prop_delta)
      || (ec->elem == elem_file && elem == elem_file_prop_delta))
    {
      svn_xml_make_open_tag (&str, pool, svn_xml_normal, "prop-delta", NULL);
      ec->elem = elem;
    }

  /* If we didn't make it to the type of element the caller asked for,
     either the caller wants us to do something we don't do or we have
     a bug. */
  assert (ec->elem == elem);

  return str;
}


/* Output XML for adding or replacing a file or directory.  Also set
   EC->elem to the value of DIRFILE for consistency.  */
static svn_error_t *
output_addreplace (struct edit_context *ec, enum elemtype addreplace,
                   enum elemtype dirfile, svn_string_t *name,
                   svn_string_t *ancestor_path, svn_revnum_t ancestor_revision)
{
  svn_string_t *str;
  apr_pool_t *pool = svn_pool_create (ec->pool);
  svn_error_t *err;
  apr_size_t len;
  apr_hash_t *att;
  const char *outertag = (addreplace == elem_add) ? "add" : "replace";
  const char *innertag = (dirfile == elem_dir) ? "dir" : "file";

  str = get_to_elem (ec, elem_tree_delta, pool);
  svn_xml_make_open_tag (&str, pool, svn_xml_normal, outertag,
                         "name", name, NULL);

  att = apr_make_hash (pool);
  if (ancestor_path != NULL)
    {
      char buf[128];
      apr_hash_set (att, "ancestor", strlen("ancestor"), ancestor_path);
      sprintf (buf, "%lu", (unsigned long) ancestor_revision);
      apr_hash_set (att, "ver", strlen("ver"), svn_string_create (buf, pool));
    }
  svn_xml_make_open_tag_hash (&str, pool, svn_xml_normal, innertag, att);

  ec->elem = dirfile;

  len = str->len;
  err = svn_stream_write (ec->output, str->data, &len);
  apr_destroy_pool (pool);
  return err;
}


/* Output a set or delete element.  ELEM is the type of prop-delta
   (elem_dir_prop_delta or elem_file_prop_delta) the element lives
   in.  This function sets EC->elem to ELEM for consistency.  */
static svn_error_t *
output_propset (struct edit_context *ec, enum elemtype elem,
                svn_string_t *name, svn_string_t *value)
{
  svn_string_t *str;
  apr_pool_t *pool = svn_pool_create (ec->pool);
  svn_error_t *err;
  apr_size_t len;

  str = get_to_elem (ec, elem, pool);
  if (value != NULL)
    {
      svn_xml_make_open_tag (&str, pool, svn_xml_protect_pcdata, "set",
                             "name", name, NULL);
      svn_xml_escape_string (&str, value, pool);
      svn_xml_make_close_tag (&str, pool, "set");
    }
  else
    svn_xml_make_open_tag (&str, pool, svn_xml_self_closing, "delete",
                           "name", name, NULL);

  len = str->len;
  err = svn_stream_write (ec->output, str->data, &len);
  apr_destroy_pool (pool);
  return err;
}


static svn_error_t *
delete_item (svn_string_t *name, void *parent_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_context *ec = db->edit_context;
  svn_string_t *str;
  apr_pool_t *pool = svn_pool_create (ec->pool);
  svn_error_t *err;
  apr_size_t len;

  str = get_to_elem (ec, elem_tree_delta, pool);
  svn_xml_make_open_tag (&str, pool, svn_xml_self_closing, "delete",
                         "name", name, NULL);

  len = str->len;
  err = svn_stream_write (ec->output, str->data, &len);
  apr_destroy_pool (pool);
  return err;
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *parent_baton,
               svn_string_t *ancestor_path,
               svn_revnum_t ancestor_revision,
               void **child_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_context *ec = db->edit_context;

  *child_baton = make_dir_baton (ec, elem_add);
  return output_addreplace (ec, elem_add, elem_dir, name,
                            ancestor_path, ancestor_revision);
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   svn_revnum_t ancestor_revision,
                   void **child_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_context *ec = db->edit_context;

  *child_baton = make_dir_baton (ec, elem_replace);
  return output_addreplace (ec, elem_replace, elem_dir, name,
                            ancestor_path, ancestor_revision);
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  struct dir_baton *db = (struct dir_baton *) dir_baton;
  struct edit_context *ec = db->edit_context;

  return output_propset (ec, elem_dir_prop_delta, name, value);
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *db = (struct dir_baton *) dir_baton;
  struct edit_context *ec = db->edit_context;
  svn_string_t *str;
  apr_size_t len;

  str = get_to_elem (ec, elem_dir, db->pool);
  if (db->addreplace != elem_delta_pkg)
    {
      /* Not the root directory.  */
      const char *outertag = (db->addreplace == elem_add) ? "add" : "replace";
      svn_xml_make_close_tag (&str, db->pool, "dir");
      svn_xml_make_close_tag (&str, db->pool, outertag);
      ec->elem = elem_tree_delta;
    }
  else
    {
      /* We're closing the root directory.  */
      ec->elem = elem_delta_pkg;
      ec->root_dir_closed = TRUE;
    }

  len = str->len;
  if (len != 0)
    SVN_ERR (svn_stream_write (ec->output, str->data, &len));
  apr_destroy_pool (db->pool);
  if (ec->root_dir_closed && ec->open_file_count == 0)
    SVN_ERR (close_edit (ec));
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *parent_baton,
          svn_string_t *ancestor_path,
          svn_revnum_t ancestor_revision,
          void **file_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_context *ec = db->edit_context;

  SVN_ERR(output_addreplace (ec, elem_add, elem_file, name,
                             ancestor_path, ancestor_revision));
  *file_baton = make_file_baton (ec, elem_add);
  ec->curfile = *file_baton;
  ec->open_file_count++;
  return SVN_NO_ERROR;
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *parent_baton,
              svn_string_t *ancestor_path,
              svn_revnum_t ancestor_revision,
              void **file_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_context *ec = db->edit_context;

  SVN_ERR(output_addreplace (ec, elem_replace, elem_file, name,
                             ancestor_path, ancestor_revision));
  *file_baton = make_file_baton (ec, elem_replace);
  ec->curfile = *file_baton;
  ec->open_file_count++;
  return SVN_NO_ERROR;
}


static svn_error_t *
output_svndiff_data (void *baton, const char *data, apr_size_t *len)
{
  struct file_baton *fb = (struct file_baton *) baton;
  struct edit_context *ec = fb->edit_context;

  /* Just pass through the write request to the editor's output stream.  */
  return svn_stream_write (ec->output, data, len);
}


static svn_error_t *
finish_svndiff_data (void *baton)
{
  struct file_baton *fb = (struct file_baton *) baton;
  struct edit_context *ec = fb->edit_context;
  apr_pool_t *subpool = svn_pool_create (ec->pool);
  svn_string_t *str = NULL;
  svn_error_t *err;
  apr_size_t slen;

  svn_xml_make_close_tag (&str, subpool, "text-delta");
  slen = str->len;
  err = svn_stream_write (ec->output, str->data, &slen);
  apr_destroy_pool (subpool);
  return err;
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t **handler,
                 void **handler_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  struct edit_context *ec = fb->edit_context;
  svn_string_t *str = NULL;
  apr_pool_t *pool = svn_pool_create (ec->pool);
  svn_error_t *err;
  apr_size_t len;
  svn_stream_t *output, *encoder;
  apr_hash_t *att;

  att = apr_make_hash (pool);
  if (fb->txdelta_id == 0)
    {
      /* We are inside a file element (possibly in a prop-delta) and
         are outputting a text-delta inline.  */
      str = get_to_elem (ec, elem_file, pool);
    }
  else
    {
      /* We should be at the end of the delta (after the root
         directory has been closed) and are outputting a deferred
         text-delta.  */
      char buf[128];
      sprintf(buf, "%d", fb->txdelta_id);
      apr_hash_set (att, "id", strlen("id"), svn_string_create (buf, pool));
    }
#ifdef QUOPRINT_SVNDIFFS
  apr_hash_set (att, "encoding", strlen("encoding"),
                svn_string_create ("quoted-printable", pool));
#endif
  svn_xml_make_open_tag_hash (&str, pool, svn_xml_protect_pcdata,
                              "text-delta", att);
  fb->txdelta_id = -1;

  len = str->len;
  err = svn_stream_write (ec->output, str->data, &len);
  apr_destroy_pool (pool);

  /* Set up a handler which will write base64-encoded svndiff data to
     the editor's output stream.  */
  output = svn_stream_create (fb, fb->pool);
  svn_stream_set_write (output, output_svndiff_data);
  svn_stream_set_close (output, finish_svndiff_data);
#ifdef QUOPRINT_SVNDIFFS
  encoder = svn_quoprint_encode (output, ec->pool);
#else
  encoder = svn_base64_encode (output, ec->pool);
#endif
  svn_txdelta_to_svndiff (encoder, ec->pool, handler, handler_baton);

  return err;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  struct edit_context *ec = fb->edit_context;

  return output_propset (ec, elem_file_prop_delta, name, value);
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  struct edit_context *ec = fb->edit_context;
  svn_string_t *str;
  apr_size_t len;

  /* Close the file element if we are still working on it.  */
  if (!fb->closed)
    {
      const char *outertag = (fb->addreplace == elem_add) ? "add" : "replace";
      str = get_to_elem (ec, elem_file, fb->pool);
      svn_xml_make_close_tag (&str, fb->pool, "file");
      svn_xml_make_close_tag (&str, fb->pool, outertag);

      len = str->len;
      SVN_ERR (svn_stream_write (ec->output, str->data, &len));
      ec->curfile = NULL;
      ec->elem = elem_tree_delta;
    }
  apr_destroy_pool (fb->pool);

  ec->open_file_count--;
  if (ec->root_dir_closed && ec->open_file_count == 0)
    SVN_ERR (close_edit (ec));
  return SVN_NO_ERROR;
}


static const svn_delta_edit_fns_t tree_editor =
{
  delete_item,
  add_directory,
  replace_directory,
  change_dir_prop,
  close_directory,
  add_file,
  replace_file,
  apply_textdelta,
  change_file_prop,
  close_file,
};


svn_error_t *
svn_delta_get_xml_editor (svn_stream_t *output,
			  const svn_delta_edit_fns_t **editor,
			  void **root_dir_baton,
			  apr_pool_t *pool)
{
  struct edit_context *ec;
  svn_string_t *str = NULL;
  apr_size_t len;

  /* Construct and initialize the editor context.  */
  ec = apr_palloc (pool, sizeof (*ec));
  ec->pool = pool;
  ec->output = output;
  ec->elem = elem_dir;
  ec->curfile = NULL;
  ec->txdelta_id_counter = 1;
  ec->open_file_count = 0;
  ec->root_dir_closed = FALSE;

  /* Now set up the editor and root baton for the caller.  */
  *editor = &tree_editor;
  *root_dir_baton = make_dir_baton (ec, elem_delta_pkg);

  /* Construct and write out the header.  This should probably be
     deferred until the first editor call.  */
  svn_xml_make_header (&str, pool);
  svn_xml_make_open_tag (&str, pool, svn_xml_normal, "delta-pkg", NULL);

  len = str->len;
  return svn_stream_write (ec->output, str->data, &len);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
