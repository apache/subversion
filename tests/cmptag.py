import os, sys, shutil

def cvs_checkout(repo, module, tag, target):
    if tag == None:
        tag = ""
    else:
        tag = " -r " + tag
    rc = os.system("cvs -q -d %s/%s co -P %s -d %s %s >>test.log" % (os.getcwd(), repo, tag, target, module))
    if rc != 0:
        raise "cvs co failed"

def do_cvs2svn(repo, module, target):
    rc = os.system("../cvs2svn.py --create -s %s %s/%s >test.log" % (target, repo, module));
    if rc != 0:
        raise "cvs2svn failed"

def svn_checkout(repo, branch, target):
    rc = os.system("svn co file://%s/%s%s %s >>test.log" % (os.getcwd(), repo, branch, target));
    if rc != 0:
        raise "svn co failed"

def check_tag(cvs_repo, module, tag, svn_repo, path):
    cvs_checkout(cvs_repo, module, tag, "cvs-co.tmp");
    svn_checkout(svn_repo, path, "svn-co.tmp");
    rc = os.system("diff -x .svn -x CVS -ru cvs-co.tmp svn-co.tmp");
    os.system("rm -rf cvs-co.tmp");
    os.system("rm -rf svn-co.tmp");
    return rc;

def check_tags(cvs_repo, module, tags):
    print "    converting %s %s:" % (cvs_repo, module)
    do_cvs2svn(cvs_repo, module, "svn-repo.tmp");
    for tag, path in tags:
        print "        checking tag %s -> %s" % (tag, path)
        check_tag(cvs_repo, module, tag, "svn-repo.tmp", path);
    os.system("rm -rf svn-repo.tmp");


#check_tags("testcvs", "test1", 
#           [
#             ( None, "/trunk" )
#           ]);
