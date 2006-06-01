package org.tigris.subversion.auth;

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

import java.util.Map;

import org.tigris.subversion.SubversionException;

/**
 * The methods of this interface correspond to the types
 * (svn_auth_provider_object_t, svn_auth_provider_t, provider_baton)
 * and functions described in the subversion C api located in
 * 'svn_auth.h'.
 */
public interface AuthProvider
{
    /**
     * @return A description of this kind of authentication provider.
     * This generally is prefixed with the namespace appropriate to
     * your application.
     */
    String getKind();

    /**
     * @param parameters Any runtime data which the provider may need,
     * or <code>null</code> if not needed.
     * @return The first valid credentials.
     */
    Object firstCredentials(Map parameters)
        throws SubversionException;

    /**
     * @param parameters Any runtime data which the provider may need,
     * or <code>null</code> if not needed.
     * @return The next valid credentials.
     */
    Object nextCredentials(Map parameters)
        throws SubversionException;

    /**
     * @param parameters Any runtime data which the provider may need,
     * or <code>null</code> if not needed.
     * @return Whether the credentials were saved.
     */
    boolean saveCredentials(Object credentials, Map parameters)
        throws SubversionException;
}
