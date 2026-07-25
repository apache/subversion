/* Username/password prompt/save using 1.9 org.apache.subversion API.

   Compile against non-installed Subversion JavaHL build:

     javac -cp subversion/bindings/javahl/classes -d subversion/bindings/javahl/classes ExampleAuthn.java

   Run:

     LD_LIBRARY_PATH=subversion/libsvn_auth_gnome_keyring/.libs java -cp subversion/bindings/javahl/classes -Djava.library.path=subversion/bindings/javahl/native/.libs ExampleAuthn <URL> <config-dir>

 */
import org.apache.subversion.javahl.*;
import org.apache.subversion.javahl.types.*;
import org.apache.subversion.javahl.remote.*;
import org.apache.subversion.javahl.callback.*;
import java.io.Console;

public class ExampleAuthn {

    protected static class MyAuthn {

      public static AuthnCallback getAuthn() {
        return new MyAuthnCallback();
      }

      private static class MyAuthnCallback
      implements AuthnCallback {

        public UserPasswordResult
        userPasswordPrompt(String realm, String username, boolean maySave) {
          System.out.println("userPasswordPrompt");
          System.out.println("Realm: " + realm);
          String prompt;
          if (username == null) {
            System.out.print("Username: ");
            username = System.console().readLine();
            prompt = "Password: ";
          } else {
            prompt = "Password for " + username + ": ";
          }
          String password = new String(System.console().readPassword(prompt));
          return new UserPasswordResult(username, password, maySave);
        }

        public boolean
        allowStorePlaintextPassword(String realm) {
          System.out.println("allowStorePlaintextPassword");
          System.out.println("Realm: " + realm);
          System.out.print("Store plaintext password on disk? (y/n): ");
          String s = System.console().readLine();
          return s.equals("y") ? true : false;
        }

        public UsernameResult
        usernamePrompt(String realm, boolean maySave) {
          System.out.println("usernamePrompt not implemented!");
          return null;
        }

        public boolean
        allowStorePlaintextPassphrase(String realm) {
          System.out.println("allowStorePlaintextPassphrase not implemented!");
          return false;
        }

        public SSLServerTrustResult
        sslServerTrustPrompt(String realm,
                             SSLServerCertFailures failures,
                             SSLServerCertInfo info,
                             boolean maySave) {
          System.out.println("sslServerTrustPrompt");
          System.out.println("(r)eject or (t)emporary?");
          String s = System.console().readLine();
          return s.equals("t") ? SSLServerTrustResult.acceptTemporarily()
                               : SSLServerTrustResult.reject();
        }

        public SSLClientCertResult
        sslClientCertPrompt(String realm, boolean maySave) {
          System.out.println("sslClientCertPrompt not implemented!");
          return null;
        }

        public SSLClientCertPassphraseResult
        sslClientCertPassphrasePrompt(String realm, boolean maySave) {
          System.out.println("sslClientCertPassphrasePrompt not implemented!");
          return null;
        }
      }
    }

  public static void main(String argv[]) {

    if (argv.length != 2) {
      System.err.println("usage: ExampleAuthn <URL> <config-dir>");
      return;
    }
    RemoteFactory factory = new RemoteFactory();
    factory.setConfigDirectory(argv[1]);
    factory.setPrompt(MyAuthn.getAuthn());
    try {
      ISVNRemote raSession = factory.openRemoteSession(argv[0]);
      raSession.getReposUUID();
    } catch (Exception e) {
      e.printStackTrace();
    }
  }
}
