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
import org.tigris.subversion.util.NativeResources;
import org.tigris.subversion.wc.Notifier;

import org.tigris.subversion.swig.client;
import org.tigris.subversion.swig.svn_client_ctx_t;
import org.tigris.subversion.swig.SWIGTYPE_p_apr_file_t;
import org.tigris.subversion.swig.SWIGTYPE_p_apr_pool_t;
import org.tigris.subversion.swig.SWIGTYPE_p_svn_opt_revision_t;

/**
 * A SWIG-based implementation of the {@link Client} interface.
 *
 * @since Subversion 0.31
 */
public class StandardClient
    implements Client
{
    static
    {
        NativeResources.getInstance().initialize();
    }

    /**
     * @see Client#getSimplePromptProvider
     */
    public AuthProvider getSimplePromptProvider(ClientPrompt prompt,
                                                int retryLimit)
    {
        throw new RuntimeException("Not implemented");
    }

    /**
     * @see Client#getUsernamePromptProvider
     */
    public AuthProvider getUsernamePromptProvider(ClientPrompt prompt,
                                                  int retryLimit)
    {
        throw new RuntimeException("Not implemented");
    }

    /**
     * @see Client#getNotifier
     */
    public Notifier getNotifier()
    {
        throw new RuntimeException("Not implemented");
    }

    /**
     * @see Client#getCommitLogReceiver
     */
    public LogMessageReceiver getCommitLogReceiver()
    {
        throw new RuntimeException("Not implemented");
    }

    /**
     * @see Client#checkout
     */
    public void checkout(String url, File path, Object revision,
                         boolean recurse)
        throws SubversionException
    {
        throw new RuntimeException("Not implemented");
    }

    /**
     * @see Client#diff
     */
    public void diff(List diffOptions, String path1, OptRevision revision1,
                     String path2, OptRevision revision2, boolean recurse,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     OutputStream output, OutputStream error)
        throws SubversionException
    {
        // FIXME: How do I instantiate one of these?
        svn_client_ctx_t clientCtx = null;

        // FIXME: Temporary placeholder for revision1 and revision2.
        SWIGTYPE_p_svn_opt_revision_t dummy = null;

        SubversionException e =
            client.svn_client_diff((String []) diffOptions.toArray
                                   (new String[diffOptions.size()]), path1,
                                   dummy /*revision1*/, path2,
                                   dummy /*revision2*/, recurse,
                                   ignoreAncestry, noDiffDeleted,
                                   // TODO: Need type conversion for
                                   // output and error.
                                   (SWIGTYPE_p_apr_file_t) null,
                                   (SWIGTYPE_p_apr_file_t) null, clientCtx,
                                   (SWIGTYPE_p_apr_pool_t) null);
        if (e != null)
        {
            throw e;
        }
    }
}
