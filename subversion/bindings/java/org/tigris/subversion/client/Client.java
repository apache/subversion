package org.tigris.subversion.client;

/*
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

import java.io.File;
import java.io.OutputStream;
import java.util.List;

import org.tigris.subversion.SubversionException;
import org.tigris.subversion.auth.AuthProvider;
import org.tigris.subversion.opt.OptRevision;
import org.tigris.subversion.wc.Notifier;

/**
 * The methods of this interface correspond to the types and functions
 * described in the subversion C api located in 'svn_client.h'.
 */
public interface Client
{
    /** Fetch an authentication provider which prompts the user for name
     * and password.
     * @param prompt A delegate for prompting
     * @param retryLimit How often to re-prompt
     * @return A <code>AuthProvider</code> 
     */
    AuthProvider getSimplePromptProvider(ClientPrompt prompt, int retryLimit);

    /** Fetch an authentication provider which prompts the user for a
     * username.
     * @param prompt A delegate for prompting
     * @param retryLimit How often to re-prompt
     * @return A <code>AuthProvider</code> 
     */
    AuthProvider getUsernamePromptProvider(ClientPrompt prompt, int retryLimit);

    Notifier getNotifier();

    LogMessageReceiver getCommitLogReceiver();

    void checkout(String url, File path, Object revision, boolean recurse)
        throws SubversionException;

    /**
     * @see <a href="http://svn.collab.net/svn-doxygen/svn__client_8h.html#a33">svn_client_diff</a>
     */
    void diff(List diffOptions, String path1, OptRevision revision1,
              String path2, OptRevision revision2, boolean recurse,
              boolean ignoreAncestry, boolean noDiffDeleted,
              OutputStream output, OutputStream error)
        throws SubversionException;
}
