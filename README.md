ucp
===

Recursive directory concatenation for use with udpipe.  High performance 'cat'-ing of directories.

******
_WARNING: In early stages of development._
******
 
Basic usage
-----------
You can pass it any number of files or directoryies and it will create file stream.  Headers in the stream specify what kind of data the receiving end is reading.  You can also redirect the stream to a file for use later.

    ucp source_dir | [pipe of your choosing] |  ucp -l dest_dir
	
Options
-------
The following options are currently supported by ucp:

    --help 		      	print this message
    --verbose 		  	verbose, notify of files being sent. Same as -v2
    --quiet 		  	silence all warnings. Same as -v0
    --pipe "pipe cmd"   ucp will tokenize and execute specified pipe command and pipe stdout to new process
    
    -u					same as --pipe
    -p 		          	print the transfer progress of each file
    -l [dest_dir] 	  	listen for file transfer and write to dest_dir [default ./]
    -v level 	      	set the level of verbosity

Levels of Verbosity
-------------------
   	0: Withhold WARNING messages
   	1: Update user on file transfers [DEFAULT]
   	2: Update user on underlying processes, i.e. directory creation
   	3: Unassigned
