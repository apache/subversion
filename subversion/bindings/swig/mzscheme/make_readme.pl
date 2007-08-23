#!/usr/bin/perl
#Find all of the globals being exported
open (IN , "cat *.c |grep scheme_add_global |");
#Write our output to the readme file
`cp README.in README`;
open (OUT, ">>README");
while (<IN>) {
  if ($_ =~ /scheme_add_global\(\"(.*?)\",.*?(\d),(\d)/o) {
    print OUT "function:$1 min args: $2 max args:$3\n";
  }
}
close (OUT);
