JDK = /usr/java/jdk1.3.1_01
JAVAC = $(JDK)/bin/javac

SOURCES = org/tigris/subversion/*.java \
          org/tigris/subversion/auth/*.java \
          org/tigris/subversion/client/*.java \
          org/tigris/subversion/opt/*.java \
          org/tigris/subversion/wc/*.java

compile:
	@if [ ! -d build ]; then mkdir build; fi
	$(JAVAC) -g -d build $(SOURCES)

clean:
	@rm -rf build
