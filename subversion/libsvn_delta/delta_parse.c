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





/* Recursively walk down delta D to find the bottommost object, and return a
   pointer to it (its type is returned in ELT_KIND) */

void *
svn_find_delta_bottom (svn_XML_elt_t *elt_kind, svn_delta_t *d)
{
  svn_edit_t *current_edit = d->edit;

  /* Start at top-level tree-delta */
  if (current_edit == NULL)
    {
      *elt_kind = svn_XML_treedelta;
      return d;   /* The deepest object is this tree delta. */

    }

  else  /* ...go deeper. */
    {
      /* Look at edit struct inside the tree-delta */
      svn_edit_content_t *current_content = current_edit->content;

      if (current_content == NULL)
        {
          *elt_kind = svn_XML_edit;
          return current_edit;  /* The deepest object is the edit */
        }

      else  /* ...go deeper. */
        {
          /* Look inside the content object */
          if (current_content->tree_delta == NULL)
            {
              *elt_kind = svn_XML_editcontent;
              return current_content; /* deepest object is the content */
            }

          else  /* ... go deeper. */
            {
              /* Since this edit_content already contains a
                 tree-delta, we better RECURSE to continue our search!  */
              return svn_find_delta_bottom (elt_kind,
                                            current_content->tree_delta);
            }
        }
    }
}





/* Append OBJECT to the end of delta D.  ELT_KIND is passed to
   guarantee sanity and allow correct pointer casting. */

svn_error_t *
svn_append_to_delta (svn_delta_digger_t *digger, 
                     void *object, 
                     svn_XML_elt_t elt_kind)
{
  svn_delta_t *d = digger->delta;

  /* Get a grip on the last object in the delta (by cdr'ing down) */
  svn_XML_elt_t bottom_kind;
  void *bottom_ptr = svn_find_delta_bottom (&bottom_kind, d);

  switch (bottom_kind)
    {
    case (svn_XML_treedelta):
      {
        /* If bottom_ptr is a treedelta, then we must be appending an
           svn_edit_t.  Sanity check.  */
        if (elt_kind != svn_XML_edit)
          return svn_create_error (SVN_ERR_MALFORMED_XML, NULL,
                                   "expecting svn_edit_t, not found!",
                                   NULL, digger->pool);
        else
          {
            svn_delta_t *this_delta = (svn_delta_t *) bottom_ptr;
            this_delta->edit = (svn_edit_t *) object;
            return SVN_NO_ERROR;
          }
      }

    case (svn_XML_edit):
      {
        /* If bottom_ptr is an edit, then we must be appending an
           svn_edit_content_t.  Sanity check.  */
        if (elt_kind != svn_XML_editcontent)
          return svn_create_error (SVN_ERR_MALFORMED_XML, NULL,
                                   "expecting svn_edit_content_t, not found!",
                                   NULL, digger->pool);
        else
          {
            svn_edit_t *this_edit = (svn_edit_t *) bottom_ptr;
            this_edit->content = (svn_edit_content_t *) object;
            return SVN_NO_ERROR;
          }
      }

    case (svn_XML_editcontent):
      {
        /* If bottom_ptr is an edit_content, then we must be appending
           one of three kinds of objects. This is the only time we
           absolutely _have_ to look at the elt_kind field. */
        svn_edit_content_t *this_content = (svn_edit_content_t *) bottom_ptr;

        switch (elt_kind)
          {
          case svn_XML_propdelta:
            {
              this_content->prop_delta = TRUE;
              return SVN_NO_ERROR;
            }
          case svn_XML_textdelta:
            {
              this_content->text_delta = TRUE;
              return SVN_NO_ERROR;
            }
          case svn_XML_treedelta:
            {
              this_content->tree_delta = (svn_delta_t *) object;
              return SVN_NO_ERROR;
            }
          default:
            {
              return 
                svn_create_error (SVN_ERR_MALFORMED_XML, NULL,
                                  "didn't find pdelta, vdelta, or textdelta!",
                                  NULL, digger->pool);
            }
          }
      }

    default:
      {
        return svn_create_error (SVN_ERR_MALFORMED_XML, NULL,
                                 "unrecognized svn_XML_elt type.",
                                 NULL, digger->pool);
      }
    }
}






/* Callback:  called whenever we find a new tag (open paren).

    The *name argument contains the name of the tag,
    and the **atts list is a dumb list of name/value pairs, all
    null-terminated Cstrings, and ending with an extra final NULL.

*/  
      
void
svn_xml_handle_start (void *userData, const char *name, const char **atts)
{
  int i;
  char *attr_name, *attr_value;

  /* Retrieve our digger structure */
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  if (strcmp (name, "tree-delta") == 0)
    {
      /* Found a new tree-delta element */

      /* Create new svn_delta_t structure here, filling in attributes */
      svn_delta_t *new_delta = apr_pcalloc (my_digger->pool, 
                                            sizeof (svn_delta_t *));

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
      svn_edit_t *new_edit = apr_pcalloc (my_digger->pool, 
                                          sizeof (svn_edit_t *));
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
      svn_edit_t *new_edit = apr_pcalloc (my_digger->pool, 
                                          sizeof (svn_edit_t *));

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
      svn_edit_t *new_edit = apr_pcalloc (my_digger->pool, 
                                          sizeof (svn_edit_t *));
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
      svn_edit_content_t *this_edit_content 
        = apr_pcalloc (my_digger->pool, 
                       sizeof (svn_edit_content_t *));

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
      svn_edit_content_t *this_edit_content 
        = apr_pcalloc (my_digger->pool, 
                       sizeof (svn_edit_content_t *));

      this_edit_content->kind = directory_type;
      
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

void svn_xml_handle_end (void *userData, const char *name)
{
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* TODO: snip the now-closed element off the delta, by setting its
     pointer to NULL after checking that it matches the open-tag it's
     trying to close. */
}



/* Callback: called whenever we find data within a tag.  
   (Of course, we only care about data within the "text-delta" tag.)  */

void svn_xml_handle_data (void *userData, const char *data, int len)
{
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* TODO: Check context of my_digger->delta, make sure that *data is
     relevant before we bother our data_handler() */

  /* TODO: see note about data handler context optimization in
     svn_delta.h:svn_delta_digger_t. */

  (* (my_digger->data_handler)) (my_digger, data, len);

}



XML_Parser
svn_delta_make_xml_parser (svn_delta_digger_t *diggy)
{
  /* Create the parser */
  XML_Parser parser = XML_ParserCreate (NULL);

  /* All callbacks should receive the delta_digger structure. */
  XML_SetUserData (parser, diggy);

  /* Register subversion-specific callbacks with the parser */
  XML_SetElementHandler (parser,
                         svn_xml_handle_start,
                         svn_xml_handle_end); 
  XML_SetCharacterDataHandler (parser, svn_xml_handle_data);

  return parser;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
