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





/* Return the newest frame of an XML stack. (The "top" of the stack.) */
static svn_delta_stackframe_t *
find_stack_newest (svn_delta_stackframe_t *frame)
{
  if (frame)
    {
      if (frame->next == NULL)
        return frame;
      else
        return find_stack_newest (frame->next);
    }
  else
    return NULL;
}




/* 
   Go to the end of DIGGER's stack, and set the state of the
   stackframe's text_delta or prop_delta flag.  (This is how we
   indicate the opening/closing of the <text-delta> and <prop-delta>
   tags.) 

   Return an error if the tag is invalid.
*/
static svn_error_t *
twiddle_edit_content_flags (svn_delta_digger_t *digger,
                            svn_boolean_t value,
                            svn_boolean_t text_p)
{
  /* Locate the newest stackframe */
  svn_delta_stackframe_t *bot_frame = find_delta_bottom (d);
  
  /* Validation.  Fail if:
     
     1.  We're not currently inside a <file> or <dir> tag, or
     2.  The new flag value is identical to the old value.
  */
  if ((bot_frame->kind != svn_XML_content)
      || (text_p && (bot_frame->inside_textdelta == value))
      || (!text_p && (bot_frame->inside_propdelta == value)))
    {
      if (value == TRUE)
        return 
          svn_create_error (SVN_ERR_MALFORMED_XML, 0,
                            "XML validation error: unexpected <text-delta> or <prop-delta>",
                            NULL, digger->pool);
      else
        return 
          svn_create_error (SVN_ERR_MALFORMED_XML, 0,
                            "XML validation error: unexpected </text-delta> or </prop-delta>",
                            NULL, digger->pool);
    }

                             
  /* Set the flag */
  if (text_p)
    bot_frame->inside_textdelta = value;
  else
    bot_frame->inside_propdelta = value;

  return SVN_NO_ERROR;
}




/* The way to officially bail out of expat. 
   
   Store ERROR in DIGGER and set all expat callbacks to NULL. (To
   understand why this works, see svn_delta_parse(). ) */
static void
signal_expat_bailout (svn_error_t *error, svn_delta_digger_t *digger)
{
  /* This will cause the current XML_Parse() call to finish quickly */
  XML_SetElementHandler (digger->parser, NULL, NULL);
  XML_SetCharacterDataHandler (digger->parser, NULL);

  /* Once outside of XML_Parse(), the existence of this field will
     cause our larger read-loop to return. */
  digger->validation_error = err;
}




/* If we get malformed XML, create an informative error message.
   (Set DESTROY_P to indicate an unexpected closure tag) */
static svn_error_t *
XML_type_error (apr_pool_t *pool, const char *name, svn_boolean_t destroy_p)
{
  if (destroy_p)
    char *msg = 
      apr_psprintf (pool, "XML validation error: got unexpected </%s>", name);

  else
    char *msg = 
      apr_psprintf (pool, "XML validation error: got unexpected <%s>", name);

  return svn_create_error (SVN_ERR_MALFORMED_XML, 0, msg, NULL, pool);
}





/* Decide if it's valid XML to append NEW_FRAME to DIGGER's stack.  If
   so, append the frame.  If not, return a validity error. (TAGNAME is
   used for error message.) */
static svn_error_t *
do_stack_append (svn_delta_digger_t *digger, 
                 svn_delta_stackframe_t *new_frame,
                 static char *tagname)
{
  apr_pool_t *pool = digger->pool;

  /* Get a grip on the youngest frame of the stack */
  svn_delta_stackframe_t *youngest_frame = find_stack_newest (digger->stack);

  if (youngest_frame == NULL)
    {
      /* The stack is empty, this is our first frame append. */
      digger->stack = new_frame;
      return SVN_NO_ERROR;
    }
  
  /* Validity check: make sure that our XML open tag logically follows
     the previous open tag. */

  if ((new_frame->tag == svn_XML_treedelta)
      && (youngest_frame->tag != svn_XML_dir))
    return XML_type_error (pool, tagname, FALSE);

  else if ( ((new_frame->tag == svn_XML_new)
             || (new_frame->tag == svn_XML_replace)
             || (new_frame->tag == svn_XML_delete))
            && (youngest_frame->tag != svn_XML_treedelta) )
        return XML_type_error (pool, tagname, FALSE);
  
  else if ((new_frame->tag == svn_XML_file)
           || (new_frame->tag == svn_XML_dir))
    {
      if ((youngest_frame->tag != svn_XML_new)
          && (youngest_frame->tag != svn_XML_replace)
          && (youngest_frame->tag != svn_XML_delete))
        return XML_type_error (digger->pool, tagname, FALSE);
    }

  /* Append doubley-linked list item */
  
  youngest_frame->next = new_frame;
  new_frame->previous = youngest_frame;

  /* Child inherits parent's baton by default */
  new_frame->baton = youngest_frame->baton;

  return SVN_NO_ERROR;
}




/* Decide if an xml closure TAGNAME is valid, by examining the
   youngest frame in DIGGER's stack.  If so, remove YOUNGEST_FRAME
   from the stack.  If not, return a validity error. */
static svn_error_t *
do_stack_remove (svn_delta_digger_t *digger, char *tagname)
{
  apr_pool_t *pool = digger->pool;

  /* Get a grip on the youngest frame of the stack */
  svn_delta_stackframe_t *youngest_frame = find_stack_newest (digger->stack);

  if (youngest_frame == NULL)
    return XML_type_error (pool, tagname, TRUE);

  /* Validity check: Make sure the kind of object we're removing (due
     to an XML TAGNAME closure) actually agrees with the type of frame
     at the bottom of the stack! */
  
  if ((strcmp (tagname, "tree-delta") == 0)
      && (youngest_frame->tag != svn_XML_treedelta))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "new") == 0)
           && (youngest_frame->tag != svn_XML_new))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "delete") == 0)
           && (youngest_frame->tag != svn_XML_del))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "replace") == 0)
           && (youngest_frame->tag != svn_XML_replace))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "file") == 0)
           && (youngest_frame->tag != svn_XML_file))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "dir") == 0)
           && (youngest_frame->tag != svn_XML_dir))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "textdelta") == 0)
           && (youngest_frame->tag != svn_XML_textdelta))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "propdelta") == 0)
           && (youngest_frame->tag != svn_XML_propdelta))
    return XML_type_error (pool, tagname, TRUE);


  /* Passed validity check, do the actual removal */

  /* Lose the pointer to the youngest frame. */
  if (youngest_frame->previous)
    youngest_frame->previous->next = NULL;

  else  /* we must be removing the only frame in the stack */
    digger->stack = NULL;
      
  return SVN_NO_ERROR;
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

  /* Create new stackframe */
  svn_delta_stackframe_t *new_frame 
    = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t));

  /* Match the new tag's name to one of Subversion's XML tags... */

  if (strcmp (name, "tree-delta") == 0)
    new_frame->tag = svn_XML_treedelta;

  else if (strcmp (name, "new") == 0)
      new_frame->tag = svn_XML_add;

  /*  Append to stack (& validate). */
  err = telescope_delta_stack (my_digger, FALSE, new_frame, name);
  if (err)
    signal_expat_bailout (err, my_digger);
  


      /* Append to stack (& validate). */
      err = telescope_delta_stack (my_digger, FALSE, new_frame, name);
      if (err)
        signal_expat_bailout (err, my_digger);
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

      /* Append this frame to the end of the stack. (& validate). */
      err = telescope_delta_stack (my_digger, FALSE, new_frame, name);
      if (err)
        signal_expat_bailout (err, my_digger);
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
        {
          signal_expat_bailout (err, my_digger);
          return;
        }

      /* This is a major svn_delta_walk event. */

      err = (*(my_digger->walker->delete)) (new_frame->name,
                                            my_digger->walk_baton,
                                            my_digger->dir_baton);
      if (err)
        signal_expat_bailout (err, my_digger);
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

      /* Append this frame to the end of the stack. (& validate) */
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



/* Return an expat parser object which uses our svn_xml_* routines
   above as callbacks.  */

static XML_Parser
svn_delta_make_xml_parser (svn_delta_digger_t *diggy)
{
  /* Create the parser */
  XML_Parser parser = XML_ParserCreate (NULL);

  /* All callbacks should receive the digger structure. */
  XML_SetUserData (parser, diggy);

  /* Register subversion-specific callbacks with the parser */
  XML_SetElementHandler (parser,
                         xml_handle_start,
                         xml_handle_end); 
  XML_SetCharacterDataHandler (parser, svn_xml_handle_data);

  return parser;
}



/* The _one_ public interface in this file; see svn_delta.h */

svn_error_t *
svn_delta_parse (svn_delta_read_fn_t *source_fn,
                 void *source_baton,
                 svn_delta_walk_t *walker,
                 void *walk_baton,
                 void *dir_baton,
                 apr_pool_t *pool)
{
  char buf[BUFSIZ];
  int len;
  int done;
  svn_error_t *err;
  XML_Parser parser;

  /* Create a digger structure */
  svn_digger_t *digger = apr_pcalloc (pool, sizeof (svn_digger_t));

  digger->pool             = pool;
  digger->stack            = NULL;
  digger->walker           = walker;
  digger->walk_baton       = walk_baton;
  digger->dir_baton        = dir_baton;
  digger->validation_error = SVN_NO_ERROR;

  /* Create a custom expat parser (one uses our own svn callbacks and
     hands them the digger on each XML event) */
  parser = svn_delta_make_xml_parser (digger);

  /* Store the parser in the digger too, so that our expat callbacks
     can magically set themselves to NULL in the case of an error. */

  digger->parser = parser;


  /* Our main parse-loop */

  do {
    /* Read BUFSIZ bytes into buf using the supplied read function. */
    len = BUFSIZ;
    err = (*(source_fn)) (source_baton, buf, &len);
    if (err)
      return 
        svn_quick_wrap_error (err, "svn_delta_parse: can't read data source");

    /* How many bytes were actually read into buf?  According to the
       definition of an svn_delta_read_fn_t, we should keep reading
       until the reader function says that 0 bytes were read. */
    done = (len == 0);
    
    /* Parse the chunk of stream. */
    if (! XML_Parse (parser, buf, len, done))
    {
      /* Uh oh, expat itself choked somehow.  Return its message. */
      svn_error_t *err
        = svn_create_error
        (SVN_ERR_MALFORMED_XML, 0,
         apr_psprintf (pool, "%s at line %d",
                       XML_ErrorString (XML_GetErrorCode (parsimonious)),
                       XML_GetCurrentLineNumber (parsimonious)),
         NULL, pool);
      XML_ParserFree (parsimonious);
      return err;
    }

    /* After parsing our chunk, check to see if any validation errors
       happened. */
    if (digger->validation_error)
      return digger->validation_error;

  } while (! done);


  /* Done parsing entire SRC_BATON stream, so clean up and return. */
  XML_ParserFree (parser);
  return SVN_NO_ERROR;
}






/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
