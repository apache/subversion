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

/*------------------------------------------------------------------*/

/** The administrative `versions' file tracks the version numbers of
files within a particular subdirectory.  Subdirectories are *not*
tracked, because the first "entry" in this file represents `.' -- and
therefore each subdir keeps track of itself.

Taken from libsvn_wc/README, the XML file format looks like:

  <wc-versions xmlns="http://subversion.tigris.org/xmlns/blah">
    <entry name="" version="5"/>     <!-- empty name means current dir -->
    <entry name="foo.c" version="3"/>
    <entry name="bar.c" version="7"/>
    <entry name="baz.c" version="6"/>
  </wc-versions>

Note that if there exists a file in text-base that is not mentioned in
the `versions' file, it is assumed to have the same version as the
parent directory.  (In the case above, version 5.)  The `versions'
file only records *exceptions* to this rule.  

-------------------------------------------------------------- **/



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
