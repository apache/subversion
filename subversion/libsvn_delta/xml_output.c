/*
 * xml_output.c:  output a Subversion "tree-delta" XML stream
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

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_io.h"
#include "svn_xml.h"
#include "svn_base64.h"
#include "svn_quoprint.h"
#include "svn_pools.h"
#include "delta.h"

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
  elem_open,
  elem_dir,
  elem_dir_prop_delta,
  elem_tree_delta,
  elem_file,
  elem_file_prop_delta
};

struct edit_baton
{
  svn_stream_t *output;
  enum elemtype elem;  /* Current element we are inside at the end of
                          a call.  One of elem_dir_prop_delta,
                          elem_tree_delta, elem_file, elem_dir, or
                          elem_file_prop_delta.  */
  struct file_baton *curfile;
  svn_revnum_t target_revision;
  apr_pool_t *pool;
  int txdelta_id_counter;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  enum elemtype addopen;     /* elem_add or elem_open, or
                                elem_delta_pkg for the root
                                directory.  */
  apr_pool_t *pool;
};


struct file_baton
{
  struct edit_baton *edit_baton;
  enum elemtype addopen;
  int txdelta_id;               /* ID of deferred text delta;
                                   0 means we're still working on the file,
                                   -1 means we already saw a text delta.  */
  int closed;			/* 1 if we closed the element already.  */
  apr_pool_t *pool;
};


/* Convenience macro. */
#define STR_REV(p,rev) \
   (apr_psprintf ((p), "%" SVN_REVNUM_T_FMT, (rev)))


static struct dir_baton *
make_dir_baton (struct edit_baton *eb, 
                enum elemtype addopen,
                apr_pool_t *pool)
{
  struct dir_baton *db = apr_palloc (pool, sizeof (*db));
  db->edit_baton = eb;
  db->addopen = addopen;
  db->pool = pool;
  return db;
}


static struct file_baton *
make_file_baton (struct edit_baton *eb, 
                 enum elemtype addopen,
                 apr_pool_t *pool)
{
  struct file_baton *fb = apr_palloc (pool, sizeof (*fb));
  fb->edit_baton = eb;
  fb->addopen = addopen;
  fb->txdelta_id = 0;
  fb->closed = 0;
  fb->pool = pool;
  return fb;
}


/* The meshing between the editor interface and the XML delta format
   is such that we can't usually output the end of an element until we
   go on to the next thing, and for a given call we may or may not
   have already output the beginning of the element we're working on.
   This function takes care of "unwinding" and "winding" from the
   current element to the kind of element we need to work on next.  We
   never have to unwind past a dir element, so the unwinding steps are
   bounded in number and easy to visualize.  The nesting of the
   elements we care about looks like:
  
        dir -> prop_delta
            -> tree_delta -> add/open -> file -> prop_delta

   We cannot be in an add/open element at the end of a call, so
   add/open and file are treated as a unit by this function.  Note
   that although there is no open or dir element corresponding to
   the root directory (the root directory's tree-delta and/or
   prop-delta elements live directly inside the delta-pkg element), we
   pretend that there is for the sake of regularity.

   This function will "unwind" arbitrarily within that little tree,
   but will only "wind" from dir to tree_delta or prop_delta or from
   file to prop_delta.  Winding through add/open/file would require
   extra information.

   ELEM specifies the element type we want to get to, with prop_delta
   split out into elem_dir_prop_delta and elem_file_prop_delta
   depending on where the prop_delta is in the little tree.  The
   element type we are currently in is recorded inside EB.  */

static svn_stringbuf_t *
get_to_elem (struct edit_baton *eb, enum elemtype elem, apr_pool_t *pool)
{
  svn_stringbuf_t *str = svn_stringbuf_create ("", pool);
  struct file_baton *fb;

  /*** Unwind.  Start from the leaves and go back as far as necessary.  */

  if ((eb->elem == elem_file_prop_delta) && (elem != elem_file_prop_delta))
    {
      svn_xml_make_close_tag (&str, pool, SVN_DELTA__XML_TAG_PROP_DELTA);
      eb->elem = elem_file;
    }

  if ((eb->elem == elem_file) 
      && (elem != elem_file) && (elem != elem_file_prop_delta))
    {
      const char *outertag = ((eb->curfile->addopen == elem_add) 
                              ? SVN_DELTA__XML_TAG_ADD 
                              : SVN_DELTA__XML_TAG_OPEN);

      fb = eb->curfile;
      if (fb->txdelta_id == 0)
        {
          const char *idstr;
          fb->txdelta_id = eb->txdelta_id_counter++;
          idstr = apr_psprintf (pool, "%d", fb->txdelta_id);
          svn_xml_make_open_tag (&str, pool, svn_xml_self_closing,
                                 SVN_DELTA__XML_TAG_TEXT_DELTA_REF,
                                 SVN_DELTA__XML_ATTR_ID, idstr, NULL);
        }
      svn_xml_make_close_tag (&str, pool, SVN_DELTA__XML_TAG_FILE);
      svn_xml_make_close_tag (&str, pool, outertag);
      fb->closed = 1;
      eb->curfile = NULL;
      eb->elem = elem_tree_delta;
    }

  if ((eb->elem == elem_tree_delta)
      && ((elem == elem_dir) || (elem == elem_dir_prop_delta)))
    {
      svn_xml_make_close_tag (&str, pool, SVN_DELTA__XML_TAG_TREE_DELTA);
      eb->elem = elem_dir;
    }

  if ((eb->elem == elem_dir_prop_delta) && (elem != elem_dir_prop_delta))
    {
      svn_xml_make_close_tag (&str, pool, SVN_DELTA__XML_TAG_PROP_DELTA);
      eb->elem = elem_dir;
    }

  /*** Now wind.  */

  if ((eb->elem == elem_dir) && (elem == elem_tree_delta))
    {
      svn_xml_make_open_tag (&str, pool, svn_xml_normal, 
                             SVN_DELTA__XML_TAG_TREE_DELTA, NULL);
      eb->elem = elem_tree_delta;
    }

  if (((eb->elem == elem_dir) && (elem == elem_dir_prop_delta))
      || ((eb->elem == elem_file) && (elem == elem_file_prop_delta)))
    {
      svn_xml_make_open_tag (&str, pool, svn_xml_normal, 
                             SVN_DELTA__XML_TAG_PROP_DELTA, NULL);
      eb->elem = elem;
    }

  /* If we didn't make it to the type of element the caller asked for,
     either the caller wants us to do something we don't do or we have
     a bug. */
  assert (eb->elem == elem);

  return str;
}


/* Output XML for adding or replacing a file or directory.  Also set
   EB->elem to the value of DIRFILE for consistency.  */
static svn_error_t *
output_addopen (struct edit_baton *eb, 
                enum elemtype addopen,
                enum elemtype dirfile, 
                const char *path,
                const char *base_path,
                svn_revnum_t base_revision,
                apr_pool_t *pool)
{
  svn_stringbuf_t *str;
  apr_size_t len;
  apr_hash_t *att;
  const char *outertag = ((addopen == elem_add) 
                          ? SVN_DELTA__XML_TAG_ADD 
                          : SVN_DELTA__XML_TAG_OPEN);
  const char *innertag = ((dirfile == elem_dir) 
                          ? SVN_DELTA__XML_TAG_DIR 
                          : SVN_DELTA__XML_TAG_FILE);
  const char *name = svn_path_basename (path, pool);

  str = get_to_elem (eb, elem_tree_delta, pool);
  svn_xml_make_open_tag (&str, pool, svn_xml_normal, outertag,
                         SVN_DELTA__XML_ATTR_NAME, name, NULL);

  att = apr_hash_make (pool);
  if ((addopen == elem_add) && (base_path != NULL))
    apr_hash_set (att, SVN_DELTA__XML_ATTR_COPYFROM_PATH, 
                  APR_HASH_KEY_STRING, base_path);

  if (SVN_IS_VALID_REVNUM (base_revision))
    {
      const char *buf = STR_REV (pool, base_revision);
      if (addopen == elem_add)
        apr_hash_set (att, SVN_DELTA__XML_ATTR_COPYFROM_REV, 
                      APR_HASH_KEY_STRING, buf);
      else
        apr_hash_set (att, SVN_DELTA__XML_ATTR_BASE_REV, 
                      APR_HASH_KEY_STRING, buf);
    }
  svn_xml_make_open_tag_hash (&str, pool, svn_xml_normal, innertag, att);

  eb->elem = dirfile;

  len = str->len;
  return svn_stream_write (eb->output, str->data, &len);
}


/* Output a set or delete element.  ELEM is the type of prop-delta
   (elem_dir_prop_delta or elem_file_prop_delta) the element lives
   in.  This function sets EB->elem to ELEM for consistency.  */
static svn_error_t *
output_propset (struct edit_baton *eb, 
                enum elemtype elem,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  svn_stringbuf_t *str;
  apr_size_t len;

  str = get_to_elem (eb, elem, pool);
  if (value)
    {
      svn_xml_make_open_tag (&str, pool, svn_xml_protect_pcdata, 
                             SVN_DELTA__XML_TAG_SET,
                             SVN_DELTA__XML_ATTR_NAME, 
                             name, NULL);
      svn_xml_escape_string (&str, value, pool);
      svn_xml_make_close_tag (&str, pool, SVN_DELTA__XML_TAG_SET);
    }
  else
    svn_xml_make_open_tag (&str, pool, svn_xml_self_closing, 
                           SVN_DELTA__XML_TAG_DELETE,
                           SVN_DELTA__XML_ATTR_NAME, name, NULL);

  len = str->len;
  return svn_stream_write (eb->output, str->data, &len);
}


static svn_error_t *
set_target_revision (void *edit_baton, 
                     svn_revnum_t target_revision)
{
  struct edit_baton *eb = edit_baton;

  /* Stick that target revision in the edit baton to be used when
     we call open_root() */
  eb->target_revision = target_revision;
  return SVN_NO_ERROR;
}
 

static svn_error_t *
open_root (void *edit_baton, 
           svn_revnum_t base_revision, 
           apr_pool_t *pool,
           void **dir_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_stringbuf_t *str = NULL;
  apr_size_t len;
  apr_hash_t *att;

  svn_xml_make_header (&str, pool);

  att = apr_hash_make (pool);
  if (SVN_IS_VALID_REVNUM (base_revision))
    apr_hash_set (att, SVN_DELTA__XML_ATTR_BASE_REV, 
                  APR_HASH_KEY_STRING, STR_REV (pool, base_revision));
  if (SVN_IS_VALID_REVNUM (eb->target_revision))
    apr_hash_set (att, SVN_DELTA__XML_ATTR_TARGET_REV, 
                  APR_HASH_KEY_STRING, STR_REV (pool, eb->target_revision));

  svn_xml_make_open_tag_hash (&str, pool, svn_xml_normal, 
                              SVN_DELTA__XML_TAG_DELTA_PKG, att);

  *dir_baton = make_dir_baton (eb, elem_delta_pkg, pool);
  eb->elem = elem_dir;

  len = str->len;
  return svn_stream_write (eb->output, str->data, &len);
}


static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t revision, 
              void *parent_baton,
              apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_stringbuf_t *str;
  apr_hash_t *att;
  apr_size_t len;
  const char *name = svn_path_basename (path, pool);

  str = get_to_elem (eb, elem_tree_delta, pool);
  att = apr_hash_make (pool);

  apr_hash_set (att, SVN_DELTA__XML_ATTR_NAME, 
                APR_HASH_KEY_STRING, name);
  if (SVN_IS_VALID_REVNUM (revision))
    apr_hash_set (att, SVN_DELTA__XML_ATTR_BASE_REV, 
                  APR_HASH_KEY_STRING, STR_REV (pool, revision));

  svn_xml_make_open_tag_hash (&str, pool, svn_xml_self_closing, 
                              SVN_DELTA__XML_TAG_DELETE, att);

  len = str->len;
  return svn_stream_write (eb->output, str->data, &len);
}


static svn_error_t *
add_directory (const char *path,
               void *parent_baton,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;

  *child_baton = make_dir_baton (eb, elem_add, pool);
  return output_addopen (eb, elem_add, elem_dir, path,
                         copyfrom_path, copyfrom_revision, pool);
}


static svn_error_t *
open_directory (const char *path,
                void *parent_baton,
                svn_revnum_t base_revision,
                apr_pool_t *pool,
                void **child_baton)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;

  *child_baton = make_dir_baton (eb, elem_open, pool);
  return output_addopen (eb, elem_open, elem_dir, path,
                         NULL, base_revision, pool);
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  return output_propset (eb, elem_dir_prop_delta, name, value, pool);
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_stringbuf_t *str;
  apr_size_t len;

  str = get_to_elem (eb, elem_dir, db->pool);
  if (db->addopen != elem_delta_pkg)
    {
      /* Not the root directory.  */
      const char *outertag = ((db->addopen == elem_add) 
                              ? SVN_DELTA__XML_TAG_ADD 
                              : SVN_DELTA__XML_TAG_OPEN);
      svn_xml_make_close_tag (&str, db->pool, SVN_DELTA__XML_TAG_DIR);
      svn_xml_make_close_tag (&str, db->pool, outertag);
      eb->elem = elem_tree_delta;
    }
  else
    eb->elem = elem_delta_pkg;

  len = str->len;
  return svn_stream_write (eb->output, str->data, &len);
}


static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;

  SVN_ERR (output_addopen (eb, elem_add, elem_file, path,
                           copyfrom_path, copyfrom_revision, pool));
  *file_baton = make_file_baton (eb, elem_add, pool);
  eb->curfile = *file_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (const char *name,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **file_baton)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;

  SVN_ERR (output_addopen (eb, elem_open, elem_file, name,
                           NULL, base_revision, pool));
  *file_baton = make_file_baton (eb, elem_open, pool);
  eb->curfile = *file_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
output_svndiff_data (void *baton, 
                     const char *data, 
                     apr_size_t *len)
{
  struct file_baton *fb = baton;
  struct edit_baton *eb = fb->edit_baton;

  /* Just pass through the write request to the editor's output stream.  */
  return svn_stream_write (eb->output, data, len);
}


static svn_error_t *
finish_svndiff_data (void *baton)
{
  struct file_baton *fb = baton;
  struct edit_baton *eb = fb->edit_baton;
  apr_pool_t *subpool = svn_pool_create (eb->pool);
  svn_stringbuf_t *str = NULL;
  svn_error_t *err;
  apr_size_t slen;

  svn_xml_make_close_tag (&str, subpool, SVN_DELTA__XML_TAG_TEXT_DELTA);
  slen = str->len;
  err = svn_stream_write (eb->output, str->data, &slen);
  svn_pool_destroy (subpool);
  return err;
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_stringbuf_t *str = NULL;
  apr_pool_t *pool = svn_pool_create (eb->pool);
  svn_error_t *err;
  apr_size_t len;
  svn_stream_t *output, *encoder;
  apr_hash_t *att;

  att = apr_hash_make (pool);
  if (fb->txdelta_id == 0)
    {
      /* We are inside a file element (possibly in a prop-delta) and
         are outputting a text-delta inline.  */
      str = get_to_elem (eb, elem_file, pool);
    }
  else
    {
      /* We should be at the end of the delta (after the root
         directory has been closed) and are outputting a deferred
         text-delta.  */
      apr_hash_set (att, SVN_DELTA__XML_ATTR_ID, 
                    APR_HASH_KEY_STRING,
                    apr_psprintf(pool, "%d", fb->txdelta_id));
    }
#ifdef QUOPRINT_SVNDIFFS
  apr_hash_set (att, SVN_DELTA__XML_ATTR_ENCODING, 
                APR_HASH_KEY_STRING, "quoted-printable");
#endif
  svn_xml_make_open_tag_hash (&str, pool, svn_xml_protect_pcdata,
                              SVN_DELTA__XML_TAG_TEXT_DELTA, att);
  fb->txdelta_id = -1;

  len = str->len;
  err = svn_stream_write (eb->output, str->data, &len);
  svn_pool_destroy (pool);

  /* Set up a handler which will write base64-encoded svndiff data to
     the editor's output stream.  */
  output = svn_stream_create (fb, fb->pool);
  svn_stream_set_write (output, output_svndiff_data);
  svn_stream_set_close (output, finish_svndiff_data);
#ifdef QUOPRINT_SVNDIFFS
  encoder = svn_quoprint_encode (output, eb->pool);
#else
  encoder = svn_base64_encode (output, eb->pool);
#endif
  svn_txdelta_to_svndiff (encoder, eb->pool, handler, handler_baton);

  return err;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  return output_propset (eb, elem_file_prop_delta, name, value, pool);
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_stringbuf_t *str;
  svn_error_t *err = SVN_NO_ERROR;
  apr_size_t len;

  /* Close the file element if we are still working on it.  */
  if (! fb->closed)
    {
      const char *outertag = ((fb->addopen == elem_add) 
                              ? SVN_DELTA__XML_TAG_ADD 
                              : SVN_DELTA__XML_TAG_OPEN);
      str = get_to_elem (eb, elem_file, fb->pool);
      svn_xml_make_close_tag (&str, fb->pool, SVN_DELTA__XML_TAG_FILE);
      svn_xml_make_close_tag (&str, fb->pool, outertag);

      len = str->len;
      err = svn_stream_write (eb->output, str->data, &len);
      eb->curfile = NULL;
      eb->elem = elem_tree_delta;
    }
  return err;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_error_t *err;
  svn_stringbuf_t *str = NULL;
  apr_size_t len;

  svn_xml_make_close_tag (&str, eb->pool, SVN_DELTA__XML_TAG_DELTA_PKG);
  len = str->len;
  err = svn_stream_write (eb->output, str->data, &len);
  if (err == SVN_NO_ERROR)
    err = svn_stream_close (eb->output);
  svn_pool_destroy (eb->pool);
  return err;
}


svn_error_t *
svn_delta_get_xml_editor (svn_stream_t *output,
			  const svn_delta_editor_t **editor,
			  void **edit_baton,
			  apr_pool_t *pool)
{
  struct edit_baton *eb;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor (pool);

  /* Construct an edit baton. */
  eb = apr_palloc (subpool, sizeof (*eb));
  eb->pool = subpool;
  eb->output = output;
  eb->curfile = NULL;
  eb->txdelta_id_counter = 1;
  eb->target_revision = SVN_INVALID_REVNUM;

  /* Construct an editor. */
  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->close_edit = close_edit;

  *edit_baton = eb;
  *editor = tree_editor;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
