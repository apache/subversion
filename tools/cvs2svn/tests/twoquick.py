import cmptag

cmptag.check_tags("cvsrepos", "twoquick", 
                  [
                      ( None, "/trunk" ),
                      ( "after", "/tags/after" ),
                      ( "before", "/tags/before" )
                  ])
