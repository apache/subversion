/*
 * delta_parse.c: create an svn_delta_t from an XML stream
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
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
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * individuals on behalf of Collab.Net.
 */

/* ==================================================================== */


/*
  This library contains callbacks to use with the "expat-lite" XML
  parser.  The callbacks produce an svn_delta_t structure from a
  stream containing Subversion's XML delta representation.

  To use this library, see "deltaparse-test.c" in tests/.
  
  Essentially, one must 
  
  * create an XML_Parser
  * register the callbacks (below) with the parser
  * call XML_Parse() on a bytestream
  
*/

#include "delta_parse.h"



/* kff todo: all these constructors need corresponding destructors. */

/* kff todo: path, version, new? */
svn_ancestor_t *
svn_delta_ancestor_create (apr_pool_t *pool)
{
  svn_ancestory_t *annie;
  anny = apr_pcalloc (pool, sizeof (*anny));
  return anny;
}

/* kff todo: kind, ancestor, etc? */
svn_edit_content_t *
svn_delta_edit_content_create (apr_pool_t *pool)
{
  svn_edit_content_t *e;
  e = apr_pcalloc (pool, sizeof (*e));
  e->prop_delta = FALSE;
  e->text_delta = FALSE;
  e->tree_delta = NULL;
  return e;
}

/* kff todo: action name, content? */
svn_edit_t *
svn_delta_edit_create (apr_pool_t *pool)
{
  svn_edit_t *eddy;
  eddy = apr_pcalloc (pool, sizeof (*eddy));
  eddy->content = NULL;
  return eddy;
}

/* kff todo: perhaps should take the context args, source_root &
   base_version, and set them in the new delta */
svn_delta_t *
svn_delta_create (apr_pool_t *pool)
{
  svn_delta_t *d;
  d = apr_pcalloc (pool, sizeof (*d));
  d->edit = NULL;
  return d;
}


/* kff todo: again, take the callbacks, and set them?  Need a policy
   w.r.t. constructors such as this. */
svn_delta_digger_t *
svn_delta_digger_create (apr_pool_t *pool)
{
  svn_delta_digger_t *diggy;
  diggy = apr_pcalloc (pool, sizeof (*diggy));
  return diggy;
}



/* Walk down a delta, append object to the end of it, whereever that
   may be.  :)   Cast the object when we attach it.  */
svn_error_t *
svn_append_to_delta (svn_delta_t *d, void *object, svn_XML_elt_t elt_kind)
{
  svn_edit_t *current_edit = d->edit;

  /* Start at top-level tree-delta */
  if (current_edit == NULL)
    {
      /* If no "current edit" in this tree-delta, attach the object! */
      /* TODO:  do a sanity check that (elt_kind == svn_XML_edit) */
      current_edit = (svn_edit_t *) object;
      return SVN_NO_ERROR;
    }
  else
    {
      /* Look inside the "current edit" of this tree-delta */
      svn_edit_content_t *current_content = current_edit->content;

      if (current_content == NULL)
        {
          /* Our object must be a content object, attach it. */
          /* TODO:  do a sanity check that (elt_kind == svn_XML_editcontent) */
          current_content = (svn_edit_content_t *) object;
          return SVN_NO_ERROR;
        }
      else
        {
          /* Look inside the content object */
          if (current_content->tree_delta != NULL)
            {
              /* If this content already contains a tree-delta, then
                 we better _recurse_ to continue our search for a
                 drop-off point! */
              svn_error_t *err = 
                svn_append_to_delta (current_content->tree_delta, 
                                     object,
                                     elt_kind);
              return err;
            }
          else 
            {
              /* Since we can't traverse any deeper... we must
                 therefore attach *object to one of the three fields
                 in this content structure.  This is the only time we
                 absolutely _have_ to look at the elt_kind field. */
              switch (elt_kind)
                {
                case svn_XML_propdelta:
                  {
                    current_content->prop_delta = TRUE;
                    return SVN_NO_ERROR;
                  }
                case svn_XML_textdelta:
                  {
                    current_content->text_delta = TRUE;
                    return SVN_NO_ERROR;
                  }
                case svn_XML_treedelta:
                  {
                    /* TODO:  do a sanity check that in fact
                       (elt_kind == svn_XML_treedelta) */
                    current_content->tree_delta = (svn_delta_t *) object;
                    return SVN_NO_ERROR;
                  }
                default:
                  {
                    /*TODO: return a nice svn_error_t struct here,
                      complete with the original expat error code
                      inside it */
                    return SVN_NO_ERROR;
                  }
                }
            }
        }
    }
}



/* Callback:  called whenever we find a new tag (open paren).

    The *name argument contains the name of the tag,
    and the **atts list is a dumb list of name/value pairs, all
    null-terminated Cstrings, and ending with an extra final NULL.

*/  
      
void
svn_xml_startElement(void *userData, const char *name, const char **atts)
{
  int i;
  char *attr_name, *attr_value;

  /* Retrieve our digger structure */
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  if (strcmp (name, "tree-delta") == 0)
    {
      /* Found a new tree-delta element */

      /* Create new svn_delta_t structure here, filling in attributes */
      svn_delta_t *new_delta = svn_delta_create (my_digger->pool);

      /* TODO: <tree-delta> doesn't take any attributes right now, but
         our svn_delta_t structure still has src_root and base_ver
         fields.  Is this bad? */

      if (my_digger->delta == NULL)
        {
          /* This is the very FIRST element of our tree delta! */
          my_digger->delta = new_delta;
          return;
        }
      else
        {
          /* This is a nested tree-delta, below a <dir>.  Hook it in. */
          svn_error_t *err = 
            svn_append_to_delta (my_digger->delta, 
                                 new_delta,
                                 svn_XML_treedelta);

          /* TODO: we're inside an event-driven callback.  What do we
             do if we get an error?  Just Punt?  Call a warning
             callback?  Perhaps we should have an error_handler()
             inside our digger structure! */
        }
    }

  else if (strcmp (name, "text-delta") == 0)
    {
      /* Found a new text-delta element */
      /* No need to create a text-delta structure... */
      /* ...just mark flag in edit_content structure (should be the
         last structure on our growing delta) */
      
      svn_error_t *err = svn_append_to_delta (my_digger->delta,
                                              NULL,
                                              svn_XML_textdelta);

      /* TODO: check error */
    }

  else if (strcmp (name, "prop-delta") == 0)
    {
      /* Found a new prop-delta element */
      /* No need to create a prop-delta structure... */
      /* ...just mark flag in edit_content structure (should be the
         last structure on our growing delta) */

      svn_error_t *err = svn_append_to_delta (my_digger->delta,
                                              NULL,
                                              svn_XML_propdelta);

      /* TODO: check error */
    }

  else if (strcmp (name, "new") == 0)
    {
      svn_error_t *err;
      /* Found a new svn_edit_t */
      /* Build a new edit struct */
      svn_edit_t *new_edit = svn_delta_edit_create (my_digger->pool);
      new_edit->kind = action_new;

      /* Our three edit tags currently only have one attribute: "name" */
      if (strcmp (*atts, "name") == 0) {
        new_edit->name = svn_string_create (++*atts, my_digger->pool);
      }
      else {
        /* TODO: return error if we have some other attribute */
      }

      /* Now drop this edit at the end of our delta */
      err = svn_append_to_delta (my_digger->delta,
                                 new_edit,
                                 svn_XML_edit);
      /* TODO: check error */
    }

  else if (strcmp (name, "replace"))
    {
      svn_error_t *err;
      /* Found a new svn_edit_t */
      /* Build a new edit struct */
      svn_edit_t *new_edit = svn_delta_edit_create (my_digger->pool);
      new_edit->kind = action_replace;

      /* Our three edit tags currently only have one attribute: "name" */
      if (strcmp (*atts, "name") == 0) {
        new_edit->name = svn_string_create (++*atts, my_digger->pool);
      }
      else {
        /* TODO: return error if we have some other attribute */
      }

      /* Now drop this edit at the end of our delta */
      err = svn_append_to_delta (my_digger->delta,
                                 new_edit,
                                 svn_XML_edit);
      /* TODO: check error */

    }

  else if (strcmp (name, "delete"))
    {
      svn_error_t *err;
      /* Found a new svn_edit_t */
      /* Build a new edit struct */
      svn_edit_t *new_edit = svn_delta_edit_create (my_digger->pool);
      new_edit->kind = action_delete;

      /* Our three edit tags currently only have one attribute: "name" */
      if (strcmp (*atts, "name") == 0) {
        new_edit->name = svn_string_create (++*atts, my_digger->pool);
      }
      else {
        /* TODO: return error if we have some other attribute */
      }

      /* Now drop this edit at the end of our delta */
      err = svn_append_to_delta (my_digger->delta,
                                 new_edit,
                                 svn_XML_edit);
      /* TODO: check error */

    }

  else if (strcmp (name, "file"))
    {
      svn_error_t *err;
      /* Found a new svn_edit_content_t */
      /* Build a edit_content_t */
      svn_edit_content_t *this_edit_content = 
        svn_delta_edit_content_create (my_digger->pool);
      this_edit_content->kind = file_type;
      
      /* Build an ancestor object out of **atts */
      while (*atts)
        {
          char *attr_name = *atts++;
          char *attr_value = *atts++;

          if (strcmp (attr_name, "ancestor") == 0)
            {
              this_edit_content->ancestor_path
                = svn_string_create (attr_value, my_digger->pool);
            }
          else if (strcmp (attr_name, "ver") == 0)
            {
              this_edit_content->ancestor_version = atoi (attr_value);
            }
          else if (strcmp (attr_name, "new") == 0)
            {
              /* Do nothing, because ancestor_path is already set to
                 NULL, which indicates a new entity. */
            }
          else
            {
              /* TODO: unknown tag attribute, return error */
            }
        }

      /* Drop the edit_content object on the end of the delta */
      err = svn_append_to_delta (my_digger->delta,
                                 this_edit_content,
                                 svn_XML_editcontent);

      /* TODO:  check for error */
    }

  else if (strcmp (name, "dir"))
    {
      svn_error_t *err;
      /* Found a new svn_edit_content_t */
      /* Build a edit_content_t */
      svn_edit_t *this_edit_content = 
        svn_delta_edit_content_create (my_digger->pool);
      this_edit_content->kind = directory_type;
      
      /* Build an ancestor object out of **atts */
      while (*atts)
        {
          char *attr_name = *atts++;
          char *attr_value = *atts++;
          svn_ancestor_t *annie = svn_delta_ancestor_create (my_digger->pool);

          if (strcmp (attr_name, "ancestor") == 0)
            {
              this_edit_content->ancestor_path
                = svn_string_create (attr_value, my_digger->pool);
            }
          else if (strcmp (attr_name, "ver") == 0)
            {
              this_edit_content->ancestor_version = atoi(attr_value);
            }
          else if (strcmp (attr_name, "new") == 0)
            {
              /* Do nothing, because NULL ancestor_path indicates a
                 new entity. */
            }
          else
            {
              /* TODO: unknown tag attribute, return error */
            }
        }

      /* Drop the edit_content object on the end of the delta */
      err = svn_append_to_delta (my_digger->delta,
                                 this_edit_content,
                                 svn_XML_editcontent);

      /* TODO:  check for error */

      /* Call the "directory" callback in the digger struct; this
         allows the client to possibly create new subdirs on-the-fly,
         for example. */
      err = (* (my_digger->dir_handler)) (my_digger, this_edit_content);

      /* TODO: check for error */
    }

  else
    {
      svn_error_t *err;
      /* Found some unrecognized tag, so PUNT to the caller's
         default handler. */
      err = (* (my_digger->unknown_elt_handler)) (my_digger, name, atts);

      /* TODO: check for error */
    }
}



/*  Callback:  called whenever we find a close tag (close paren) */

void svn_xml_endElement(void *userData, const char *name)
{
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* TODO: snip the now-closed element off the delta, by setting its
     pointer to NULL after checking that it matches the open-tag it's
     trying to close. */
}



/* Callback: called whenever we find data within a tag.  
   (Of course, we only care about data within the "text-delta" tag.)  */

void svn_xml_DataHandler(void *userData, const char *data, int len)
{
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* TODO: Check context of my_digger->delta, make sure that *data is
     relevant before we bother our data_handler() */

  (* (my_digger->data_handler)) (my_digger, data, len);

}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
