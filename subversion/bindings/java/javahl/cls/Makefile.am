jar_content = ${wildcard org/tigris/subversion/javahl/*.class } \
                ${wildcard org/tigris/subversion/javahl/test/*.class} 

svnjavahl.jar : $(jar_content)
	@JDK@/bin/jar cf svnjavahl.jar .

data_DATA = svnjavahl.jar

clean : 
	rm -r org svnjavahl.jar
