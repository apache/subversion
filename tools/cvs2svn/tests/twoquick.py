import cmptag

cmptag.check_tags("testcvs1", "twoquick", 
                  [
                      ( None, "/trunk" ),
                      ( "after", "/tags/after" ),
                      ( "before", "/tags/before" )
                  ]);
