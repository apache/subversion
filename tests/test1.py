import cmptag

cmptag.check_tags("testcvs1", "test1", 
                  [
                      # trunk
                      ( None, "/trunk" ),
                      # trunk tag
                      ( "icewm_1-2-0-pre2", "/tags/icewm_1-2-0-pre2" ),
                      # branch point
                      ( "icewm-1-2-ROOT", "/tags/icewm-1-2-ROOT" ),
                      # branch head
                      ( "icewm-1-2-BRANCH", "/branches/icewm-1-2-BRANCH" ),
                      # branch tag
                      ( "icewm-1_2_2", "/tags/icewm-1_2_2" ),
                  ]);
