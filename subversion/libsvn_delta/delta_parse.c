/*
 * delta_parse.c:  build an svn_delta_stackframe_t from an XML stream
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
  parser.  The callbacks maintain state by building & retracting an
  svn_delta_stackframe_t structure from a stream containing
  Subversion's XML delta representation.

  Essentially, one must 
  
  * create an Subversion-specific XML_Parser with svn_delta_xml_parser()

  * call XML_Parse(parser) on a bytestream
  
*/

#include <stdio.h>
#include <string.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "apr_strings.h"
#include "xmlparse.h"






/* Loop through expat's ATTS variable and fill in relevant fields in
   our stackframe FRAME. */
static void
fill_attributes (apr_pool_t *pool,
                 svn_delta_stackframe_t *frame,
                 const char **atts)
{
  while (*atts)
    {
      const char *attr_name = *atts++;
      const char *attr_value = *atts++;
      
      if (strcmp (attr_name, "ancestor") == 0)
        {
          frame->ancestor_path
            = svn_string_create (attr_value, pool);
        }
      else if (strcmp (attr_name, "ver") == 0)
        {
          frame->ancestor_ver = atoi (attr_value);
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




/* Heh, by creating our "stackframe" structure, this routine has been
   reduced to a trivial nothingness, like a lisp routine or something.
   :) */

/* Return the last frame of a delta stack. */
static svn_delta_stackframe_t *
find_delta_bottom (svn_delta_stackframe_t *frame)
{
  if (frame)
    {
      if (frame->next == NULL)
        return frame;
      else
        return find_delta_bottom (frame->next);
    }
  else
    return NULL;
}




/* 
   Go to bottom of frame D, and set the state of the content's
   text_delta or prop_delta flag. 
*/
static void
twiddle_edit_content_flags (svn_delta_stackframe_t *d, 
                            svn_boolean_t value,
                            svn_boolean_t text_p)
{
  svn_delta_stackframe_t *bot_frame = find_delta_bottom (d);
  
  /* ...just mark flag in edit_content structure (should be the
     last structure on our growing delta) */
  if (text_p)
    bot_frame->inside_textdelta = value;
  else
    bot_frame->inside_propdelta = value;
}





/* If we get malformed XML, return an informative error saying so. */
static svn_error_t *
XML_type_error (apr_pool_t *pool, const char *name, svn_boolean_t destroy_p)
{
  if (destroy_p)
    {
      char *msg = 
        apr_psprintf (pool, "Typecheck error in starpend_delta: trying to remove frame type '%s', but current bottom of stackframe is different type!", name);
      return svn_create_error (SVN_ERR_MALFORMED_XML, 0,
                               msg, NULL, pool);
    }

  else
    {
      char *msg = 
        apr_psprintf (pool, "Typecheck error in starpend_delta: trying to append frame type '%s', but current bottom of stackframe is wrong type!", name);
      return svn_create_error (SVN_ERR_MALFORMED_XML, 0,
                               msg, NULL, pool);
    }
}



/* 
   starpend_delta() : either (ap)pend or (un)pend a frame to the
                          end of a delta.  

   Append or remove NEW_FRAME to/from the end of delta-stackframe
   within DIGGER.

   Use DESTROY-P to toggle append/remove behavior.

   This routine *type-checks* frames being added or removed.  The TAGNAME
   argument is used to type-check a frame being removed;
   NEW_FRAME->kind is used to type-check a frame being added.

*/

static svn_error_t *
telescope_delta_stack (svn_delta_digger_t *digger,
                       const svn_boolean_t destroy_p,
                       svn_delta_stackframe_t *new_frame,
                       const char *tagname)
{
  /* Get a grip on the bottommost and penultimate frames in our
     digger's stack. */

  svn_delta_stackframe_t *bot_frame
    = find_delta_bottom (digger->stack);

  if (destroy_p)  /* unpend procedure... */
    {
      /* Type-check: make sure the kind of object we're removing (due
         to an XML closure tag) actually agrees with the type of frame
         at the bottom of the stack! */
      if ((strcmp (tagname, "tree-delta") == 0)
          && (bot_frame->kind != svn_XML_tree))
        return XML_type_error (digger->pool, tagname, TRUE);
      else if (((strcmp (tagname, "new") == 0) 
                || (strcmp (tagname, "replace") == 0)
                || (strcmp (tagname, "delete") == 0))
               && (bot_frame->kind != svn_XML_edit))
        return XML_type_error (digger->pool, tagname, TRUE);
      else if (((strcmp (tagname, "file") == 0) 
                || (strcmp (tagname, "dir") == 0))
               && (bot_frame->kind != svn_XML_content))
        return XML_type_error (digger->pool, tagname, TRUE);

      /* We're using pools, so just "lose" the pointer to the
         bottommost frame. */
      if (bot_frame->previous)
        bot_frame->previous->next = NULL;
      else
        digger->stack = NULL;

      return SVN_NO_ERROR;
    }

  else /* append procedure... */
    {
      /* Type-check: make sure the kind of object we're adding (due to
         an XML open tag) actually connects properly with the type of
         frame at the bottom of the stack! */
      if ((strcmp (tagname, "tree-delta") == 0)
          && (bot_frame->kind != svn_XML_content))
        return XML_type_error (digger->pool, tagname, FALSE);
      else if (((strcmp (tagname, "new") == 0) 
                || (strcmp (tagname, "replace") == 0)
                || (strcmp (tagname, "delete") == 0))
               && (bot_frame->kind != svn_XML_tree))
        return XML_type_error (digger->pool, tagname, FALSE);
      else if (((strcmp (tagname, "file") == 0) 
                || (strcmp (tagname, "dir") == 0))
               && (bot_frame->kind != svn_XML_edit))
        return XML_type_error (digger->pool, tagname, FALSE);

      bot_frame->next = new_frame;
      new_frame->previous = bot_frame;
      return SVN_NO_ERROR;
    }
}





/* Callback:  called whenever we find a new tag (open paren).

   The *name argument contains the name of the tag,
   and the **atts list is a dumb list of name/value pairs, all
   null-terminated Cstrings, and ending with an extra final NULL.

*/  
      
static void
xml_handle_start (void *userData, const char *name, const char **atts)
{
  svn_error_t *err;

  /* Resurrect our digger structure */
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* Match the new tag's name to one of Subversion's XML tags... */

  if (strcmp (name, "tree-delta") == 0)
    {
      /* Found a new tree-delta element */

      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t));

      new_frame->kind = svn_XML_tree;
      
      if (my_digger->stack == NULL)
        /* This is the very FIRST element of our tree delta! */
        my_digger->stack = new_frame;
      else  /* This is a nested tree-delta.  Hook it in. */
        {
          err = telescope_delta_stack (my_digger, FALSE, new_frame, name);
          if (err)
            svn_handle_error (err, stderr);
        }
    }

  else if (strcmp (name, "text-delta") == 0)
    {
      /* Found a new text-delta element */
      /* No need to create a text-delta structure... */

      twiddle_edit_content_flags (my_digger->stack, TRUE, TRUE);
    }

  else if (strcmp (name, "prop-delta") == 0)
    {
      /* Found a new prop-delta element */
      /* No need to create a prop-delta structure... */

      twiddle_edit_content_flags (my_digger->stack, TRUE, FALSE);
    }

  else if (strcmp (name, "new") == 0)
    {
      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t));

      new_frame->kind = svn_XML_edit;
      new_frame->edit_kind = svn_edit_add;

      /* Fill in the stackframe's attributes from **atts */
      fill_attributes (my_digger->pool, new_frame, atts);

      /* Append this frame to the end of the stack. */
      err = telescope_delta_stack (my_digger, FALSE, new_frame, name);
      if (err)
        svn_handle_error (err, stderr);
    }

  else if (strcmp (name, "replace") == 0)
    {
      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t));

      new_frame->kind = svn_XML_edit;
      new_frame->edit_kind = svn_edit_replace;

      /* Fill in the stackframe's attributes from **atts */
      fill_attributes (my_digger->pool, new_frame, atts);

      /* Append this frame to the end of the stack. */
      err = telescope_delta_stack (my_digger, FALSE, new_frame, name);
      if (err)
        svn_handle_error (err, stderr);
    }

  else if (strcmp (name, "delete") == 0)
    {
      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t));

      new_frame->kind = svn_XML_edit;
      new_frame->edit_kind = svn_edit_del;

      /* Fill in the stackframe's attributes from **atts */
      fill_attributes (my_digger->pool, new_frame, atts);

      /* Append this frame to the end of the stack. */
      err = telescope_delta_stack (my_digger, FALSE, new_frame, name);
      if (err)
        svn_handle_error (err, stderr);
    }

  else if (strcmp (name, "file") == 0)
    {
      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t));

      new_frame->kind = svn_XML_content;
      new_frame->content_kind = svn_content_file;

      /* Fill in the stackframe's attributes from **atts */
      fill_attributes (my_digger->pool, new_frame, atts);

      /* Append this frame to the end of the stack. */
      err = telescope_delta_stack (my_digger, FALSE, new_frame, name);
      if (err)
        svn_handle_error (err, stderr);
    }

  else if (strcmp (name, "dir") == 0)
    {
      /* Create new stackframe */
      svn_delta_stackframe_t *new_frame 
        = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t));

      new_frame->kind = svn_XML_content;
      new_frame->content_kind = svn_content_dir;

      /* Fill in the stackframe's attributes from **atts */
      fill_attributes (my_digger->pool, new_frame, atts);

      /* Append this frame to the end of the stack. */
      err = telescope_delta_stack (my_digger, FALSE, new_frame, name);
      if (err)
        svn_handle_error (err, stderr);

      /* Call the "directory" callback in the digger struct; this
         allows the client to possibly create new subdirs on-the-fly,
         for example. */
      if (my_digger->dir_handler)
        (* (my_digger->dir_handler)) (my_digger, new_frame);
    }

  else
    {
      svn_error_t *err;
      /* Found some unrecognized tag, so PUNT to the caller's
         default handler. */
      if (my_digger->unknown_elt_handler) {
        err = (* (my_digger->unknown_elt_handler)) (my_digger, name);
        if (err)
          svn_handle_error (err, stderr);
      }

    }
}






/*  Callback:  called whenever we find a close tag (close paren) */

static void 
xml_handle_end (void *userData, const char *name)
{
  svn_error_t *err;
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  
  /* Figure out what kind of element is being "closed" in our
     XML stream */

  if ((strcmp (name, "tree-delta") == 0) 
      || (strcmp (name, "new") == 0) 
      || (strcmp (name, "replace") == 0)
      || (strcmp (name, "delete") == 0)
      || (strcmp (name, "file") == 0)
      || (strcmp (name, "dir") == 0))
    {
      /* Snip the bottommost frame off of the stackframe */
      err = telescope_delta_stack (my_digger, TRUE, NULL, name);
      if (err)
        svn_handle_error (err, stderr);
    }

  else if (strcmp (name, "text-delta") == 0)
    {
      /* bottomost object of delta should be an edit_content_t,
         so we UNSET it's text_delta flag here. */

      twiddle_edit_content_flags (my_digger->stack, FALSE, TRUE);
    }

  else if (strcmp (name, "prop-delta") == 0)
    {
      /* bottomost object of delta should be an edit_content_t,
         so we UNSET it's prop_delta flag here. */

      twiddle_edit_content_flags (my_digger->stack, FALSE, FALSE);
    }

  else  /* default */
    {
      /* Found some unrecognized tag, so PUNT to the caller's
         default handler. */
      if (my_digger->unknown_elt_handler) {
        err = (* (my_digger->unknown_elt_handler)) (my_digger, name);
        if (err)
          svn_handle_error (err, stderr);
      }
    }
}



/* Callback: called whenever we find data within a tag.  
   (Of course, we only care about data within the "text-delta" tag.)  */

static void 
svn_xml_handle_data (void *userData, const char *data, int len)
{
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* TODO: Check context of my_digger->delta, make sure that *data is
     relevant before we bother our data_handler() */

  /* TODO: see note about data handler context optimization in
     svn_delta.h:svn_delta_digger_t. */

  if (my_digger->data_handler)
    (* (my_digger->data_handler)) (my_digger, data, len);
}


/* The one public interface in this file: return an expat parser
   object which uses our svn_xml_* routines above as callbacks.  */

XML_Parser
svn_delta_make_xml_parser (svn_delta_digger_t *diggy)
{
  /* Create the parser */
  XML_Parser parser = XML_ParserCreate (NULL);

  /* All callbacks should receive the delta_digger structure. */
  XML_SetUserData (parser, diggy);

  /* Register subversion-specific callbacks with the parser */
  XML_SetElementHandler (parser,
                         xml_handle_start,
                         xml_handle_end); 
  XML_SetCharacterDataHandler (parser, svn_xml_handle_data);

  return parser;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
