import org.tigris.subversion.lib.*;
import java.util.Hashtable;
import java.util.Enumeration;

public class TestStatus extends Object
{
  public static void main(String[] args)
  {
    Client client;

    try 
      {
	  Hashtable hashtable;

	  System.out.println("trying to get Client...");
	  client = Factory.getClient();
 	
	  System.out.println("sucess\nrun status...");
	  hashtable = client.status(".", true, false, false);

	  System.out.println("success\nand here the result:\n");

	  if( hashtable != null )
	  {
	    Enumeration enu=hashtable.elements();
	    int i=0;

	    while( enu.hasMoreElements() )
	      {
		String value=(String)enu.nextElement();
		System.out.println(value);
		i++;
	      }

	      System.out.println("hashtable is non-zero");
	  }
      }
    catch(Exception e)
      {
        e.printStackTrace();
      }

  }

}
