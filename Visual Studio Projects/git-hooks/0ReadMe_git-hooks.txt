This dirctory contains a set of git hook scripts which are useful when 
working with this repository to make the git commit id available when 
building simulators to uniquely identify the inputs to that build.  

It is ONLY useful for folks who clone this as a repository and build
in the working directory of that repository.

Folks who download zip or tarball archives of the repository have the 
git commit-id automatically inserted into the sim_rev.h file when the
archive is created due to a substitution performed via this repository's
.gitattributes.

To use these scripts automatically, copy these files to the .git/hooks 
directory of the current repository.  This can be done with the 
following commands:

    $ cd Visual\ Studio\ Projects/git-hooks
    $ chmod +x post*
    $ cp post* ../../.git/hooks/

Note:  The makefile AND the Visual Studio Projects automatically
       will install these git hooks in the ../../.git/hooks/ directory
       if they're not already there and execute them as needed.
