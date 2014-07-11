ucp
===

High performance recursive directory file transfer using UDT with restart capabilities. _Remote to local transfers currently under development_.

******
_WARNING: In early stages of development._ 
******
 
Basic usage
-----------
Basic usage is similar to scp 

    ucp list of files or directories host:destdir
    
will transfer the the files or directories {ucp, list, of, files, or, directories} into the remote directory destdir recursively.  

As an added functionality, ucp has the ability to restart/rerun previous transers. If a file has been modified since the last it was logged, then ucp will by default resend the file. You have the option of logging without restarting, restarting without logging, or doing both (-k). The command

    ucp -k xfer.log source host:dest
    
Will restart a transfer logged in xfer.log from directory source to directory dest on remote host.

Installation
------------

From the root ucp directory run the following:

    make
    sudo make install

	
Options
-------
The following options are currently supported by ucp:

    --help                   print uasge message
    --all-files              allows ucp to send all file types, i.e. character devices, named pipes, etc.
    --checkpoint l_file      same as (-r xfer.log -l xfer.log).  Restart and write to checkpoint.
    --ignore-modification    will not resend logged files that have been since modified
    --log xfer.log           will output completed file report to l_file
    --no-mmap                opt-out of memory mapping the file write
    --restart l_file         will restart the transfer from a log file xfer.log 
    --verbose                verbose, notify of files being sent. Same as -v2
    --quiet                  silence all warnings. Same as -v0
    

    -c ucp_src               specify the ucp source binary path
    -k                       same as --checkpoint
    -l xfer.log              same as --log 
    -r xfer.log              same as --restart
    -v level                 set the level of verbosity
    -x                       silence transfer progress    


Levels of Verbosity
-------------------
   	0: Withhold WARNING messages
   	1: Update user on file transfers [DEFAULT]
   	2: Update user on underlying processes, i.e. directory creation
   	3: Unassigned
