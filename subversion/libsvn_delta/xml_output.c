/*
 * xml_output.c:  output a Subversion "tree-delta" XML stream
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
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

struct edit_baton
{
  svn_write_fn_t *output;
  void *output_baton;
  enum elemtype elem;           /* Current element we are inside at
                                   the end of a call.  One of
                                   elem_dir, elem_dir_prop_delta,
                                   elem_tree_delta, elem_file, or
                                   elem_file_prop_delta.  */
  struct file_baton *curfile;
  apr_pool_t *pool;
  int txdelta_id_counter;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  enum elemtype addreplace;     /* elem_add or elem_replace, or
                                   elem_delta_pkg for the root
                                   directory.  */
  apr_pool_t *pool;
};


struct file_baton
{
  struct edit_baton *edit_baton;
  enum elemtype addreplace;
  int txdelta_id;               /* ID of deferred text delta;
                                   0 means we're still working on the file,
                                   -1 means we already saw a text delta.  */
  int closed;			/* 1 if we closed the element already.  */
  apr_pool_t *pool;
};


static struct dir_baton *
make_dir_baton (struct edit_baton *eb, enum elemtype addreplace)
{
  apr_pool_t *subpool = svn_pool_create (eb->pool, NULL);
  struct dir_baton *db = apr_palloc (subpool, sizeof (*db));

  db->edit_baton = eb;
  db->addreplace = addreplace;
  db->pool = subpool;
  return db;
}


static struct file_baton *
make_file_baton (struct edit_baton *eb, enum elemtype addreplace)
{
  apr_pool_t *subpool = svn_pool_create (eb->pool, NULL);
  struct file_baton *fb = apr_palloc (subpool, sizeof (*fb));

  fb->edit_baton = eb;
  fb->addreplace = addreplace;
  fb->txdelta_id = 0;
  fb->closed = 0;
  fb->pool = subpool;
  return fb;
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
            -> tree_delta -> {add/replace -> file} -> prop_delta

   (We cannot be in an add/replace element at the end of a call, so
   add/replace and file are treated as a unit by this function.)

   This function will "unwind" arbitrarily within that little tree,
   but will only "wind" from dir to tree_delta or prop_delta or from
   file to prop_delta.  Winding through add/replace/file would require
   extra information.

   ELEM specifies the element type we want to get to, with prop_delta
   split out into elem_dir_prop_delta and elem_file_prop_delta
   depending on where the prop_delta is in the little tree.  The
   element type we are currently in is recorded inside EB.  */

static svn_string_t *
get_to_elem (struct edit_baton *eb, enum elemtype elem, apr_pool_t *pool)
{
  svn_string_t *str = svn_string_create ("", pool);
  struct file_baton *fb;
  char buf[128];

  /* Unwind.  Start from the leaves and go back as far as necessary.  */
  if (eb->elem == elem_file_prop_delta && elem != elem_file_prop_delta)
    {
      svn_string_appendcstr (str, "</prop-delta>\n", pool);
      eb->elem = elem_file;
    }
  if (eb->elem == elem_file && elem != elem_file
      && elem != elem_file_prop_delta)
    {
      fb = eb->curfile;
      if (fb->txdelta_id == 0)
        {
          fb->txdelta_id = eb->txdelta_id_counter++;
          sprintf (buf, "<text-delta-ref id='%d'/>\n", fb->txdelta_id);
          svn_string_appendcstr (str, buf, pool);
        }
      svn_string_appendcstr (str, "</file>", pool);
      svn_string_appendcstr (str, ((fb->addreplace == elem_add) ? "</add>\n"
                                   : "</replace>\n"), pool);
      fb->closed = 1;
      eb->curfile = NULL;
      eb->elem = elem_tree_delta;
    }
  if (eb->elem == elem_tree_delta
      && (elem == elem_dir || elem == elem_dir_prop_delta))
    {
      svn_string_appendcstr (str, "</tree-delta>\n", pool);
      eb->elem = elem_dir;
    }
  if (eb->elem == elem_dir_prop_delta && elem != elem_dir_prop_delta)
    {
      svn_string_appendcstr (str, "</prop-delta>\n", pool);
      eb->elem = elem_dir;
    }

  /* Now wind.  */
  if (eb->elem == elem_dir && elem == elem_tree_delta)
    {
      svn_string_appendcstr (str, "<tree-delta>\n", pool);
      eb->elem = elem_tree_delta;
    }
  if (eb->elem == elem_dir && elem == elem_dir_prop_delta)
    {
      svn_string_appendcstr (str, "<prop-delta>\n", pool);
      eb->elem = elem_dir_prop_delta;
    }
  if (eb->elem == elem_file && elem == elem_file_prop_delta)
    {
      svn_string_appendcstr (str, "<prop-delta>\n", pool);
      eb->elem = elem_file_prop_delta;
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
output_addreplace (struct edit_baton *eb, enum elemtype addreplace,
                   enum elemtype dirfile, svn_string_t *name,
                   svn_string_t *ancestor_path, svn_vernum_t ancestor_version)
{
  svn_string_t *str;
  apr_pool_t *pool = svn_pool_create (eb->pool, NULL);
  svn_error_t *err;
  apr_size_t len;

  str = get_to_elem (eb, elem_tree_delta, pool);
  svn_string_appendcstr (str, "<", pool);
  svn_string_appendcstr (str, ((addreplace == elem_add) ? "add"
                               : "replace"), pool);
  svn_string_appendcstr (str, " name='", pool);
  svn_string_appendstr (str, svn_xml_escape_string (name, pool), pool);
  svn_string_appendcstr (str, "'><", pool);
  svn_string_appendcstr (str, (dirfile == elem_dir) ? "dir" : "file", pool);
  if (ancestor_path != NULL)
    {
      char buf[128];
      svn_string_appendcstr (str, " ancestor='", pool);
      svn_string_appendstr (str, svn_xml_escape_string (ancestor_path, pool),
			    pool);
      sprintf (buf, "' ver='%lu'", (unsigned long) ancestor_version);
      svn_string_appendcstr (str, buf, pool);
    }
  svn_string_appendcstr (str, ">\n", pool);

  eb->elem = dirfile;

  len = str->len;
  err = eb->output (eb->output_baton, str->data, &len, pool);
  apr_destroy_pool (pool);
  return err;
}


/* Output a set or delete element.  ELEM is the type of prop-delta
   (elem_dir_prop_delta or elem_file_prop_delta) the element lives
   in.  This function sets EB->elem to ELEM for consistency.  */
static svn_error_t *
output_propset (struct edit_baton *eb, enum elemtype elem,
                svn_string_t *name, svn_string_t *value)
{
  svn_string_t *str;
  apr_pool_t *pool = svn_pool_create (eb->pool, NULL);
  svn_error_t *err;
  apr_size_t len;

  str = get_to_elem (eb, elem, pool);
  if (value != NULL)
    {
      svn_string_appendcstr (str, "<set name='", pool);
      svn_string_appendstr (str, svn_xml_escape_string (name, pool), pool);
      svn_string_appendcstr (str, "'>", pool);
      svn_string_appendstr (str, svn_xml_escape_string (value, pool), pool);
      svn_string_appendcstr (str, "</set>\n", pool);
    }
  else
    {
      svn_string_appendcstr (str, "<delete name='", pool);
      svn_string_appendstr (str, svn_xml_escape_string (name, pool), pool);
      svn_string_appendcstr (str, "'/>\n", pool);
    }

  len = str->len;
  err = eb->output (eb->output_baton, str->data, &len, eb->pool);
  apr_destroy_pool (pool);
  return err;
}


static svn_error_t *
replace_root (void *edit_baton,
              void **dir_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  const char *hdr = "<?xml version='1.0' encoding='utf-8'?>\n<delta-pkg>\n";
  apr_size_t len = strlen(hdr);

  *dir_baton = make_dir_baton (eb, elem_delta_pkg);

  eb->elem = elem_dir;
  return eb->output (eb->output_baton, hdr, &len, eb->pool);
}


static svn_error_t *
delete (svn_string_t *name, void *parent_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_string_t *str;
  apr_pool_t *pool = svn_pool_create (eb->pool, NULL);
  svn_error_t *err;
  apr_size_t len;

  str = get_to_elem (eb, elem_tree_delta, pool);
  svn_string_appendcstr (str, "<delete name='", pool);
  svn_string_appendstr (str, svn_xml_escape_string (name, pool), pool);
  svn_string_appendcstr (str, "'/>\n", pool);

  len = str->len;
  err = eb->output (eb->output_baton, str->data, &len, eb->pool);
  apr_destroy_pool (pool);
  return err;
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *parent_baton,
               svn_string_t *ancestor_path,
               svn_vernum_t ancestor_version,
               void **child_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = db->edit_baton;

  *child_baton = make_dir_baton (eb, elem_add);
  return output_addreplace (eb, elem_add, elem_dir, name,
                            ancestor_path, ancestor_version);
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   svn_vernum_t ancestor_version,
                   void **child_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = db->edit_baton;

  *child_baton = make_dir_baton (eb, elem_replace);
  return output_addreplace (eb, elem_replace, elem_dir, name,
                            ancestor_path, ancestor_version);
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  struct dir_baton *db = (struct dir_baton *) dir_baton;
  struct edit_baton *eb = db->edit_baton;

  return output_propset (eb, elem_dir_prop_delta, name, value);
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *db = (struct dir_baton *) dir_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_string_t *str;
  svn_error_t *err;
  apr_size_t len;

  str = get_to_elem (eb, elem_dir, db->pool);
  if (db->addreplace != elem_delta_pkg)
    {
      /* Not the root directory.  */
      svn_string_appendcstr (str, "</dir>", db->pool);
      svn_string_appendcstr (str, ((db->addreplace == elem_add) ? "</add>"
                                   : "</replace>"), db->pool);
      svn_string_appendcstr (str, "\n", db->pool);
      eb->elem = elem_tree_delta;
    }
  else
    eb->elem = elem_delta_pkg;

  len = str->len;
  err = eb->output (eb->output_baton, str->data, &len, db->pool);
  apr_destroy_pool (db->pool);
  return err;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *parent_baton,
          svn_string_t *ancestor_path,
          svn_vernum_t ancestor_version,
          void **file_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = db->edit_baton;

  *file_baton = make_file_baton (eb, elem_add);
  eb->curfile = *file_baton;
  return output_addreplace (eb, elem_add, elem_file, name,
                            ancestor_path, ancestor_version);
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *parent_baton,
              svn_string_t *ancestor_path,
              svn_vernum_t ancestor_version,
              void **file_baton)
{
  struct dir_baton *db = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = db->edit_baton;

  *file_baton = make_file_baton (eb, elem_replace);
  eb->curfile = *file_baton;
  return output_addreplace (eb, elem_replace, elem_file, name,
                            ancestor_path, ancestor_version);
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *baton)
{
  struct file_baton *fb = (struct file_baton *) baton;
  struct edit_baton *eb = fb->edit_baton;
  apr_size_t len;

  /* We need a delta->vcdiff conversion function before we can output
     anything real here.  For now, just output the new data in the
     window.  This will work for "fake" delta windows that simply
     include an insert_new instruction, but not for real deltas such
     as the ones generated by svn_txdelta ().  */
  if (window != NULL)
    {
      len = window->new->len;
      return eb->output (eb->output_baton, window->new->data, &len, eb->pool);
    }
  else
    {
      const char *msg = "</text-delta>\n";
      len = strlen(msg);
      return eb->output (eb->output_baton, msg, &len, eb->pool);
    }
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t **handler,
                 void **handler_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_string_t *str;
  apr_pool_t *pool = svn_pool_create (eb->pool, NULL);
  svn_error_t *err;
  apr_size_t len;

  if (fb->txdelta_id == 0)
    {
      /* We are inside a file element (possibly in a prop-delta) and
         are outputting a text-delta inline.  */
      str = get_to_elem (eb, elem_file, pool);
      svn_string_appendcstr (str, "<text-delta>", pool);
    }
  else
    {
      /* We should be at the end of the delta (after the root
         directory has been closed) and are outputting a text-delta
         inline.  */
      char buf[128];
      sprintf(buf, "<text-delta id='%d'>", fb->txdelta_id);
      str = svn_string_create (buf, pool);
    }
  fb->txdelta_id = -1;

  *handler = window_handler;
  *handler_baton = fb;

  len = str->len;
  err = eb->output (eb->output_baton, str->data, &len, pool);
  apr_destroy_pool (pool);
  return err;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  struct edit_baton *eb = fb->edit_baton;

  return output_propset (eb, elem_file_prop_delta, name, value);
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_string_t *str;
  svn_error_t *err = SVN_NO_ERROR;
  apr_size_t len;

  /* Close the file element if we are still working on it.  */
  if (!fb->closed)
    {
      str = get_to_elem (eb, elem_file, fb->pool);
      svn_string_appendcstr (str, "</file>", fb->pool);
      svn_string_appendcstr (str, ((fb->addreplace == elem_add) ? "</add>\n"
                                   : "</replace>\n"),
                             fb->pool);

      len = str->len;
      err = eb->output (eb->output_baton, str->data, &len, fb->pool);
      eb->curfile = NULL;
      eb->elem = elem_tree_delta;
    }
  apr_destroy_pool (fb->pool);
  return err;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  const char *msg = "</delta-pkg>\n";
  apr_size_t len = strlen(msg);
  svn_error_t *err;

  err = eb->output (eb->output_baton, msg, &len, eb->pool);
  apr_destroy_pool (eb->pool);
  return err;
}


static const svn_delta_edit_fns_t tree_editor =
{
  replace_root,
  delete,
  add_directory,
  replace_directory,
  change_dir_prop,
  close_directory,
  add_file,
  replace_file,
  apply_textdelta,
  change_file_prop,
  close_file,
  close_edit
};


svn_error_t *
svn_delta_get_xml_editor (svn_write_fn_t *output,
			  void *output_baton,
			  const svn_delta_edit_fns_t **editor,
			  void **edit_baton,
			  apr_pool_t *pool)
{
  struct edit_baton *eb;
  apr_pool_t *subpool = svn_pool_create (pool, NULL);

  *editor = &tree_editor;
  eb = apr_palloc (subpool, sizeof (*edit_baton));
  eb->pool = subpool;
  eb->output = output;
  eb->output_baton = output_baton;
  eb->curfile = NULL;
  eb->txdelta_id_counter = 1;

  *edit_baton = eb;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
