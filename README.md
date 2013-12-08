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
    
    
More docmuentation to come, but the following command:

    ucp -g log_f -k log_f source_dir -o localhost:dest/dir
    
will transfer the directory into the directory dest/dir recursively.  It will log each successfull file in the file log_f.  If a previous transfer has used the file log_f as a checkpoint, the transfer will resume from the last complete file.
	
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
    -x					print xfer progress
    -o host:dest        specify the output on a remote host
    -c ucp_src          specify the ucp source binary path
    --log/-g l_file     will output completed file report to l_file
    --restart/-k l_file   will restart the transfer from a previously written log file [not mutually exclusive with -g]

Levels of Verbosity
-------------------
   	0: Withhold WARNING messages
   	1: Update user on file transfers [DEFAULT]
   	2: Update user on underlying processes, i.e. directory creation
   	3: Unassigned
