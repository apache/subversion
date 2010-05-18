/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005 CollabNet.  All rights reserved.
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
 * @endcopyright
 */

package org.tigris.subversion.javahl;

/**
 * <p>The interface for requesting authentication credentials from the
 * user.  Should the javahl bindings need the matching information,
 * these methodes will be called.</p>
 *
 * <p>This callback can also be used to provide the equivalent of the
 * <code>--no-auth-cache</code> and <code>--non-interactive</code>
 * arguments accepted by the command-line client.</p>
 */
public interface PromptUserPassword3 extends PromptUserPassword2
{
    /**
     * Request a user name and password from the user, and (usually)
     * store the auth credential caching preference specified by
     * <code>maySave</code> (used by {@link #userAllowedSave()}).
     * Applications wanting to emulate the behavior of
     * <code>--non-interactive</code> will implement this method in a
     * manner which does not require user interaction (e.g. a no-op
     * which assumes pre-cached auth credentials).
     *
     * @param realm The realm from which the question originates.
     * @param username The name of the user in <code>realm</code>.
     * @param maySave Whether caching of credentials is allowed.
     * Usually affects the return value of the {@link
     * #userAllowedSave()} method.
     * @return Whether the prompt for authentication credentials was
     * successful (e.g. in a GUI application whether the dialog box
     * was canceled).
     */
    public boolean prompt(String realm, String username, boolean maySave);

    /**
     * Ask the user a question, and (usually) store the auth
     * credential caching preference specified by <code>maySave</code>
     * (used by {@link #userAllowedSave()}).  Applications wanting to
     * emulate the behavior of <code>--non-interactive</code> will
     * implement this method in a manner which does not require user
     * interaction (e.g. a no-op).
     *
     * @param realm The realm from which the question originates.
     * @param question The text of the question.
     * @param showAnswer Whether the answer may be displayed.
     * @param maySave Whether caching of credentials is allowed.
     * Usually affects the return value of the {@link
     * #userAllowedSave()} method.
     * @return              answer as entered or null if canceled
     */
    public String askQuestion(String realm, String question,
                              boolean showAnswer, boolean maySave);

    /**
     * @return Whether the caller allowed caching of credentials the
     * last time {@link #prompt(String, String, boolean)} was called.
     * Applications wanting to emulate the behavior of
     * <code>--no-auth-cache</code> will probably always return
     * <code>false</code>.
     */
    public boolean userAllowedSave();
}
