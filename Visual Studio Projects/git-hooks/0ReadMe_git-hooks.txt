This dirctory contains a set of git hook scripts which are useful when 
working with this repository to make the git commit id available when 
building simulators to uniquely identify the inputs to that build.  

It is ONLY useful for folks who clone this as a repository and build
in the working directory of that repository.

Folks who download zip archives of the repository do not currently
have the ability to get the commit id which the archive is a snapshot 
of.  Work is being done to solve this issue for uses of the archive
snapshots as well.

To use these scripts automatically, copy these files to the .git/hooks 
directory of the current repository.  This can be done with the 
following commands:

    $ cd Visual\ Studio\ Projects/git-hooks
    $ chmod +x post*
    $ cp post* ../../.git/hooks/



Note:  You ONLY need to copy these hook scripts once for a particular 
       clone of the repository.  Once these are installed they will 
       survive as you pull subsequent revisions from the github repo.

       If you clone the repository to another platform, then you'll 
       need to copy the hooks to the .git/hooks directory again.
