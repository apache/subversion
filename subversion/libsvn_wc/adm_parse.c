/*
 * adm_parse.c: reading/writing the XML in the working copy adm area.
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
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include "xmlparse.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_wc.h"
#include "wc.h"



/*** Initialization routines. ***/

svn_string_t *
svn_wc__versions_init_contents (svn_vernum_t version, apr_pool_t *pool)
{
  svn_string_t *ret;

  /* kff todo: after writing the new element tag output routines in
     xml.c, replace the hardcode below with the proper calls. */

  const char *part_1 = 
    "<wc-versions xmlns=\"http://subversion.tigris.org/xmlns/\">\n"
    "   <entry version=\"";
  const char *part_2 =
    "\"/>\n"
    "</wc-versions>\n";

  /* Is this lame or what? */
  char buf[1000];

  ret = svn_string_create (part_1, pool);
  sprintf (buf, "%ld", (long int) version);
  svn_string_appendbytes (ret, buf, strlen (buf), pool);
  svn_string_appendbytes (ret, part_2, strlen (part_2), pool);

  return ret;
}



/*** XML parsing ***/

#if 0

/* This will be the userData for the Expat callbacks. */
struct version_mod
{
  svn_string_t *name;
  svn_vernum_t old_version;
  svn_vernum_t new_version;
  int action;  /* From the enum in delta.h: svn_delta_{add,delete,replace} */
};



/* The way to officially bail out of expat. */
static void
signal_expat_bailout (svn_error_t *error, struct version_mod *vmod)
{
  /* kff todo */
}


/* Callback: invoked whenever expat finds a new "open" tag.
 * 
 * NAME holds the name of the tag.
 * 
 * ATTS is the tag's attributes -- a list of alternating names and
 * values, all null terminated C strings, and the ATTS itself ends
 * with a null pointer as well.
 */ 
static void
xml_handle_start (void *userData, const char *tagname, const char **atts)
{
  svn_error_t *err;
  const char *value;
  struct version_mod *vmod = (struct version_mod *) userData;

  if (strcmp (tagname, "wc-versions") == 0)
    {
      /* kff todo: found beginning of the file.  No need to do
         anything with it, though? */
    }
  else if (strcmp (tagname, "entry") == 0)
    {
      const char *entry_name;
      const char *entry_version;

      entry_name = get_attribute_value ("name", atts);
      entry_version = get_attribute_value ("version", atts);

      if (entry_name)  /* Name is some file in this directory. */
        {
          /* kff todo fooo: working here
           *
           * Filter through.  The vmod struct needs to hold dest file
           * ptr as well (it will be a tmp file).  As we roll through,
           * see if this entry is the one discussed in vmod.  If it
           * is, spit out a new entry with the appropriate action;
           * else, spit out the current entry as-is, name for name and
           * attribute for attribute.  WRITE SOME XML HELPERS in
           * libsvn_subr/xml.c for this!
           */
        }
      else             /* Absent name means this directory itself. */
        {
          /* see above */
        }
    }
  else  /* unrecognized tag, bail */
    {
      signal_expat_bailout (err, vmod);
      return;
    }
}


/* Return an expat parser object for the `versions' file. */
static XML_Parser
make_xml_parser (struct version_mod *vmod)
{
  /* Create the parser */
  XML_Parser parser = XML_ParserCreate (NULL);

  /* All callbacks should receive the digger structure. */
  XML_SetUserData (parser, vmod);

  /* Register our one callback with the parser */
  XML_SetElementHandler (parser, xml_handle_start, NULL); 

  return parser;
}


svn_error_t *
svn_wc__parse_versions (svn_delta_read_fn_t *source_fn,
                        void *source_baton,
                        const svn_delta_walk_t *walker,
                        void *walk_baton,
                        void *dir_baton,
                        apr_pool_t *pool)
{
  char buf[BUFSIZ];
  apr_off_t len;
  int done;
  svn_error_t *err = NULL;
  XML_Parser expat_parser;

  /* Create a digger structure */
  svn_xml__digger_t *digger
    = apr_pcalloc (pool, sizeof (svn_xml__digger_t));

  digger->pool             = pool;
  digger->stack            = NULL;
  digger->walker           = walker;
  digger->walk_baton       = walk_baton;
  digger->dir_baton        = dir_baton;
  digger->validation_error = SVN_NO_ERROR;
  digger->vcdiff_parser    = NULL;

  /* Create a custom expat parser (uses our own svn callbacks and
     hands them the digger on each XML event) */
  expat_parser = make_xml_parser (digger);

  /* Store the parser in the digger too, so that our expat callbacks
     can magically set themselves to NULL in the case of an error. */

  digger->expat_parser = expat_parser;


  /* Our main parse-loop */

  do {
    /* Read BUFSIZ bytes into buf using the supplied read function. */
    len = BUFSIZ;
    err = (*(source_fn)) (source_baton, buf, &len, digger->pool);
    if (err)
      {
        err = svn_quick_wrap_error (err,
                                    "svn_delta_parse: can't read data source");
        goto error;
      }

    /* How many bytes were actually read into buf?  According to the
       definition of an svn_delta_read_fn_t, we should keep reading
       until the reader function says that 0 bytes were read. */
    done = (len == 0);
    
    /* Parse the chunk of stream. */
    if (! XML_Parse (expat_parser, buf, len, done))
    {
      /* Uh oh, expat *itself* choked somehow.  Return its message. */
      err = svn_create_error
        (SVN_ERR_MALFORMED_XML, 0,
         apr_psprintf (pool, "%s at line %d",
                       XML_ErrorString (XML_GetErrorCode (expat_parser)),
                       XML_GetCurrentLineNumber (expat_parser)),
         NULL, pool);
      goto error;
    }

    /* After parsing our chunk, check to see if anybody called
       signal_expat_bailout() */
    if (digger->validation_error)
      {
        err = digger->validation_error;
        goto error;
      }

  } while (! done);


 error:
  /* Done parsing entire SRC_BATON stream, so clean up and return. */
  XML_ParserFree (expat_parser);

  return err;
}


#endif /* 0 */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
