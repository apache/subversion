package org.tigris.subversion.auth;

/*
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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

import org.tigris.subversion.SubversionException;

/**
 * The methods of this interface correspond to the types
 * (svn_auth_baton_t, svn_auth_iterstate_t) and functions described in
 * the subversion C api located in 'svn_auth.h'.
 */
public interface Authenticator
{
    /**
     * This object already represents the
     * <code>svn_auth_baton_t</code> type.
     */
    void open(AuthProvider[] providers);

    void setParameter(String name, Object value);

    Object getParameter(String name);

    Object firstCredentials(String credKind)
        throws SubversionException;

    Object nextCredentials()
        throws SubversionException;

    void saveCredentials()
        throws SubversionException;
}
