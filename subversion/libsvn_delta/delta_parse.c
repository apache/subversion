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





/* Loop through expat's ATTS variable and fill in relevant fields in
   our stackframe FRAME. */
void
svn_fill_attributes (apr_pool_t *pool,
                     svn_delta_stackframe_t *frame,
                     char **atts)
{
  while (*atts)
    {
      char *attr_name = *atts++;
      char *attr_value = *atts++;
      
      if (strcmp (attr_name, "ancestor") == 0)
        {
          frame->ancestor_path
            = svn_string_create (attr_value, pool);
        }
      else if (strcmp (attr_name, "ver") == 0)
        {
          frame->ancestor_version = atoi (attr_value);
        }
      else if (strcmp (attr_name, "new") == 0)
        {
          /* Do nothing, because ancestor_path is already set to
             NULL, which indicates a new entity. */
        }
      else if (strcmp (attr_name, "name") == 0)
        {
          frame->name = svn_string_create (attr_value, pool);
        }
      else
        {
          /* TODO: unknown tag attribute, ignore for now.  */
        }
    }
}



/* For code factorization: 

   Go to bottom of frame D, and set the state of the content's
   text_delta or prop_delta flag. 

*/
void
svn_twiddle_edit_content_flags (svn_delta_stackframe_t *d, 
                                svn_boolean_t value,
                                svn_boolean_t text_p)
{
  svn_delta_stackframe *bot_frame = svn_delta_find_bottom (d);
  
  /* ...just mark flag in edit_content structure (should be the
     last structure on our growing delta) */
  if (text_p)
    bot_frame->inside_textdelta = value;
  else
    bot_frame->inside_propdelta = value;
}





/* Recursively walk down delta D.  (PARENT is used for recursion purposes.)

   Return the bottommost object in BOTTOM_OBJ and BOTTOM_KIND.
      (Needed later for appending objects to the delta.)

   The penultimate object is returned in PENULT_OBJ and PENULT_KIND. 
      (Needed later for removing objects from the delta.)
*/

void
svn_walk_delta (svn_delta_t *delta,
                fish see-prototype-for-callback,
                void *user_data)
{
  /* kff todo: finish this, then implement svn_find_delta_bottom()
     with it. */
}


/* Heh, by creating our "stackframe" structure, this routine has been
   reduced to a trivial nothingness, like a lisp routine or something.
   :) */

svn_delta_stackframe_t *
svn_find_delta_bottom (svn_delta_stackframe_t *frame)
{
  if (frame->next == NULL)
    return frame;
  else
    return svn_find_delta_bottom (frame);
}





/* 
   svn_starpend_delta() : either (ap)pend or (un)pend a frame to the
                          end of a delta.  

   (Feel free to think of a better name: svn_telescope_delta() ?) :)

   Append or remove NEW_FRAME to/from the end of delta-stackframe
   within DIGGER.

   Use DESTROY-P to toggle append/remove behavior.

*/

void
svn_starpend_delta (svn_delta_digger_t *digger,
                    svn_delta_stackframe_t *new_frame;
                    svn_boolean_t destroy-p)
{
  svn_delta_stackframe_t *bot_frame = 
    svn_find_delta_bottom (digger->frame);

  if (destroy-p)
    {
      svn_delta_stackframe_t *penultimate_frame = bot_frame->previous;

      /* We're using pools, so just "lose" the pointer to the
         bottommost frame. */
      penultimate_frame->next = NULL;
      return;
    }

  else /* append */
    {
      bot_frame->next = new_frame;
      return;
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

  /* Resurrect our digger structure */
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* Match the new tag's name to one of Subversion's XML tags... */

  if (strcmp (name, "tree-delta") == 0)
    {
      /* Found a new tree-delta element */

      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t *));
      
      if (my_digger->frame->next == NULL)
        /* This is the very FIRST element of our tree delta! */
        my_digger->frame = new_frame;
      else  /* This is a nested tree-delta.  Hook it in. */
        svn_starpend_delta (my_digger, new_frame, FALSE);
    }

  else if (strcmp (name, "text-delta") == 0)
    {
      /* Found a new text-delta element */
      /* No need to create a text-delta structure... */

      svn_twiddle_edit_content_flags (my_digger->frame, TRUE)
    }

  else if (strcmp (name, "prop-delta") == 0)
    {
      /* Found a new prop-delta element */
      /* No need to create a prop-delta structure... */

      svn_twiddle_edit_content_flags (my_digger->frame, FALSE)
    }

  else if (strcmp (name, "new") == 0)
    {
      svn_error_t *err;

      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t *));

      new_frame->edit_kind = svn_edit_add;

      /* Fill in the stackframe's attributes from **atts */
      svn_fill_attributes (my_digger->pool, new_frame, atts);

      /* Append this frame to the end of the stack. */
      svn_starpend_delta (my_digger, new_frame, FALSE);
    }

  else if (strcmp (name, "replace"))
    {
      svn_error_t *err;

      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t *));

      new_frame->edit_kind = svn_edit_replace;

      /* Fill in the stackframe's attributes from **atts */
      svn_fill_attributes (my_digger->pool, new_frame, atts);

      /* Append this frame to the end of the stack. */
      svn_starpend_delta (my_digger, new_frame, FALSE);
    }

  else if (strcmp (name, "delete"))
    {
      svn_error_t *err;

      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t *));

      new_frame->edit_kind = svn_edit_del;

      /* Fill in the stackframe's attributes from **atts */
      svn_fill_attributes (my_digger->pool, new_frame, atts);

      /* Append this frame to the end of the stack. */
      svn_starpend_delta (my_digger, new_frame, FALSE);

    }

  else if (strcmp (name, "file"))
    {
      svn_error_t *err;
      /* Found a new svn_edit_content_t */

      /* Build a edit_content_t */
      svn_edit_content_t *this_edit_content 
        = svn_create_edit_content (my_digger->pool, svn_file_type, atts);

      /* Drop the edit_content object on the end of the delta */
      err = svn_starpend_delta (my_digger, this_edit_content,
                                svn_XML_editcontent, FALSE);

      /* TODO:  check for error */
    }

  else if (strcmp (name, "dir"))
    {
      svn_error_t *err;
      /* Found a new svn_edit_content_t */

      /* Build a edit_content_t */
      svn_edit_content_t *this_edit_content 
        = svn_create_edit_content (my_digger->pool, svn_directory_type, atts);

      /* Drop the edit_content object on the end of the delta */
      err = svn_starpend_delta (my_digger, this_edit_content,
                                svn_XML_editcontent, FALSE);

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
  svn_error_t *err;
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  
  /* First, figure out what kind of element is being "closed" in our
     XML stream */

  if (strcmp (name, "tree-delta") == 0)
    {
      /* Snip the now-closed tree off the delta. */
      err = svn_starpend_delta (my_digger, NULL, svn_XML_treedelta, TRUE);
    }

  else if (strcmp (name, "text-delta") == 0)
    {
      /* TODO */
      /* bottomost object of delta should be an edit_content_t,
         so we unset it's text_delta flag here. */

      err =svn_twiddle_edit_content_flags (my_digger->delta, FALSE, TRUE);
    }

  else if (strcmp (name, "prop-delta") == 0)
    {
      /* TODO */
      /* bottomost object of delta should be an edit_content_t,
         so we unset it's prop_delta flag here. */

      err = svn_twiddle_edit_content_flags (my_digger->delta, FALSE, FALSE);
    }

  else if ((strcmp (name, "new") == 0) 
           || (strcmp (name, "replace") == 0)
           || (strcmp (name, "delete") == 0))
    {
      /* Snip the now-closed edit_t off the delta. */
      err = svn_starpend_delta (my_digger, NULL, svn_XML_edit, TRUE);
    }

  else if ((strcmp (name, "file") == 0)
           || (strcmp (name, "dir") == 0))
    {
      /* Snip the now-closed edit_content_t off the delta. */
      err = svn_starpend_delta (my_digger, NULL, svn_XML_editcontent, TRUE);
    }

  else  /* default */
    {
      /* Found some unrecognized tag, so PUNT to the caller's
         default handler. */
      err = (* (my_digger->unknown_elt_handler)) (my_digger, name, atts);
    }

  /* TODO: what to do with a potentially returned
     SVN_ERR_MALFORMED_XML at this point?  Do we need to longjump out
     of expat's callback, or does expat have a error system? */  
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
