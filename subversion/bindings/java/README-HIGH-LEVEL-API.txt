
             Subversion High Level Java API
             ===============================
             
 $LastChangedDate$
 
 Contents:

	I. ABOUT
	II. STATUS
	III. WHY INTERFACES?
	IV. WHY NOT 100% PURE JAVA?
	V. WHY SWIG?
	VI. TODO


I.     ABOUT

This is the proposed High Level Java API for Subversion. It has been designed to enable
easy implementation of Gui Clients and Plugins for Subversion.
It provides a minimal but complete set of interfaces, that map the Subversion C-Api to Java.
This interface can be implemented by various implementations.

One reference implementation is already provided. It implements the API via a thin layer that uses
JNI. Alternative implementations that can implement this interface can be based on SWIG or on
a parser of the command line client.
Please direct discussions of the API to the mailto:dev@subversion.tigris.org.
Discussions of the implementation should be in dev@svnup.tigris.org

There are two projects that use this API. They can be found at
http://svnup.tigris.org
One is the svnup Swing GUI and the other the svnup plugin for Idea.
A plugin for eclipse is in the works.


II. STATUS
	
The interface is complete and in beta. Any function that is possible through the command line client
can be done through the API. The reference implementation will be maintained, to follow
changes in the C-Client-API.
The reference implemetation provides the resources to build it on Windows and Unix.  

III. WHY INTERFACES?

There have been discussions which approach is best to connect subversion with Java software.
However it has been agreed by all, that a common API should be agreed on if possible.
While the group developing the SWIG based implementation follow a bottom up approach
we have taken a top down approach. We tried to define the kind of interface required for a gui
or a ide plugin.

IV. WHY NOT 100% PURE JAVA?

100% pure Java would require reimplementing the whole subversion Client library in Java. 
This would mean a lot of duplication and it would not be clear which client implementation
is the reference. However we believe that there should be one primary client API and that should be
the C-API.

	
V. WHY JNI?

If you know JNI it is probably the fastest approach, because it allows to handle the various
internal structures of the C-Client-API directly in C while exposing more natural interfaces to the Java programmer.
	
VI. TODO

Integration into subversion build systems for Windows and Unix.
JUnit test cases
Code style and Javadoc
