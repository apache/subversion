/*
 * versions.c :  manipulating the administrative `versions' file.
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



#include "wc.h"
#include "svn_xml.h"


/*------------------------------------------------------------------*/

/** Overview **/

/* The administrative `versions' file tracks the version numbers of
   files within a particular subdirectory.  Subdirectories are *not*
   tracked, because subdirs record their own version information.
   
   See the section on the `versions' file in libsvn_wc/README, for
   concrete information about the XML format.
   
   Note that if there exists a file in text-base that is not mentioned
   in the `versions' file, it is assumed to have the same version as
   the parent directory.  The `versions' file always mentions files
   whose version is different from the dir's, and may (but is not
   required to) mention files that are at the same version as the dir.
   
   In practice, this parser tries to filter out non-exceptions as it
   goes, so the `versions' file is always left without redundancies.
*/

/*--------------------------------------------------------------- */

/** xml callbacks **/

/* For a given ENTRYNAME in PATH's versions file, set the entry's
 * version to VERSION.  Also set other XML attributes via varargs:
 * key, value, key, value, etc, terminated by a single NULL.  (The
 * keys are char *'s and values are svn_string_t *'s.)
 * 
 * If no such ENTRYNAME exists, create it.
 */
/* Called whenever we find an <open> tag of some kind */
static void
xml_handle_start (void *userData, const char *name, const char **atts)
{
  /* There are only two kinds of tags to examine */

  if (! strcmp (name, "wc-versions"))
    {
    }

  else if (! strcmp (name, "entry"))
    {
    }

  else
    {
      /* just ignore unrecognized tags */
    }

}


/* Called whenever we find a <close> tag of some kind */
static void 
xml_handle_end (void *userData, const char *name)
{
  /* There are only two kinds of tags to examine */

  if (! strcmp (name, "wc-versions"))
    {
    }

  else if (! strcmp (name, "entry"))
    {
    }

  else
    {
      /* just ignore unrecognized tags */
    }


}



/*----------------------------------------------------------------------*/

/** Public Interfaces **/


/* For a given ENTRYNAME in PATH, set its version to VERSION in the
   `versions' file.  Also set other XML attributes via varargs: name,
   value, name, value, etc. -- where names are char *'s and values are
   svn_string_t *'s.   Terminate list with NULL. 

   If no such ENTRYNAME exists, create it.
 */
svn_error_t *svn_wc__set_versions_entry (svn_string_t *path,
                                         apr_pool_t *pool,
                                         const char *entryname,
                                         svn_vernum_t version,
                                         ...)
{
  va_list argptr;

  apr_file_t *infile = NULL;
  apr_file_t *outfile = NULL;

  /* Create a custom XML parser */




  return SVN_NO_ERROR;
}


/* For a given ENTRYNAME in PATH, read the `version's file and get its
   version into VERSION.  Also get other XML attributes via varargs:
   name, value, name, value, etc. -- where names are char *'s and
   values are svn_string_t *'s.  Terminate list with NULL.  */
svn_error_t *svn_wc__get_versions_entry (svn_string_t *path,
                                         apr_pool_t *pool,
                                         const char *entryname,
                                         svn_vernum_t *version,
                                         ...)
{
  return SVN_NO_ERROR;
}


/* Remove ENTRYNAME from PATH's `versions' file. */
svn_error_t *svn_wc__remove_versions_entry (svn_string_t *path,
                                            apr_pool_t *pool,
                                            const char *entryname)
{
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
