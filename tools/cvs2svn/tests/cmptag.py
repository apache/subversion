import os, sys, shutil, stat

def cvs_checkout(repo, module, tag, target):
    if tag == None:
        tag = ""
    else:
        tag = " -r " + tag
    rc = os.system("cvs -q -d %s/%s co -P %s -d %s %s >>test.log"
                   % (os.getcwd(), repo, tag, target, module))
    if rc != 0:
        raise "cvs co failed"

def do_cvs2svn(repo, module, target):
    print "    converting %s %s:" % (repo, module)
    rc = os.system("%s ../cvs2svn.py --create -s %s %s/%s >test.log"
                   % (sys.executable, target, repo, module))
    if rc != 0:
        raise "cvs2svn failed"

def clean_cvs2svn():
    if os.path.exists('cvs2svn-data.revs'):
        os.unlink('cvs2svn-data.revs')
    if os.path.exists('cvs2svn-data.tags'):
        os.unlink('cvs2svn-data.tags')
    if os.path.exists('cvs2svn-data.resync'):
        os.unlink('cvs2svn-data.resync')
    if os.path.exists('cvs2svn-data.c-revs'):
        os.unlink('cvs2svn-data.c-revs')
    if os.path.exists('cvs2svn-data.s-revs'):
        os.unlink('cvs2svn-data.s-revs')

def svn_checkout(repo, branch, target):
    rc = os.system("svn co file://%s/%s%s %s >>test.log"
                   % (os.getcwd(), repo, branch, target))
    if rc != 0:
        raise "svn co failed"

def get_exec_flag((results, base_dir), dirname, fnames):
    """os.path.walk() callback to build a list of files.

    walker arg is (result_list, base_directory)

    We append 2-tuples to result_list: relative_path, attributes
    """
    relative_dir = dirname[len(base_dir):]

    # don't recurse into CVS or .svn
    try:
        fnames.remove('CVS')
    except ValueError:
        pass
    try:
        fnames.remove('.svn')
    except ValueError:
        pass

    # accumulate the data
    for fn in fnames:
        mode = os.stat(os.path.join(dirname, fn))[stat.ST_MODE]
        results.append((os.path.join(relative_dir, fn),
                        mode & stat.S_IXUSR))

def cmp_attr(dir1, dir2):
    attr1 = []
    attr2 = []
    os.path.walk(dir1, get_exec_flag, (attr1, dir1))
    os.path.walk(dir2, get_exec_flag, (attr2, dir2))
    attr1.sort()
    attr2.sort()
    if attr1 != attr2:
       print repr(attr1), repr(attr2)
       raise "mismatch in file attributes"

def check_tag(cvs_repo, module, tag, svn_repo, path):
    cvs_checkout(cvs_repo, module, tag, "cvs-co.tmp")
    svn_checkout(svn_repo, path, "svn-co.tmp")
    rc = os.system("diff -x .svn -x CVS -ru cvs-co.tmp svn-co.tmp")
    cmp_attr("cvs-co.tmp", "svn-co.tmp")
    shutil.rmtree("cvs-co.tmp")
    shutil.rmtree("svn-co.tmp")
    return rc


def check_tags(cvs_repo, module, tags):
    # Clean up from any previous runs first.
    if os.path.exists('svn-repo.tmp'):
        shutil.rmtree('svn-repo.tmp')
    if os.path.exists('svn-repo.tmp'):
        os.unlink('test.log')
    clean_cvs2svn()

    do_cvs2svn(cvs_repo, module, 'svn-repo.tmp')
    for tag, path in tags:
        print "        checking tag %s -> %s" % (tag, path)
        check_tag(cvs_repo, module, tag, "svn-repo.tmp", path)
