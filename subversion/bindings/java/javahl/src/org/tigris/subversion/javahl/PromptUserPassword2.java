package org.tigris.subversion.javahl;

/**
 * Created by IntelliJ IDEA.
 * User: patrick
 * Date: Oct 18, 2003
 * Time: 9:14:34 AM
 * To change this template use Options | File Templates.
 */
public interface PromptUserPassword2 extends PromptUserPassword
{
    public static final int Reject = 0;
    public static final int AccecptTemporary = 1;
    public static final int AcceptPermanently = 2;
    public int askTrustSSLServer(String info, boolean allowPermanently);
}
