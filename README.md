Parcel
===

High performance recursive directory file transfer using UDT with restart capabilities.

******
_NEW_
******
Remote to local transfers are implemented but not tested, remote to local transfers do not currently support checkpoint restarts.

******
_WARNING: In early stages of development._
******

Basic usage
-----------
Basic usage is similar to scp

    parcel list of files or directories host:destdir

will transfer the the files or directories {parcel, list, of, files, or, directories} into the remote directory destdir recursively.

As an added functionality, parcel has the ability to restart/rerun previous transers. If a file has been modified since the last it was logged, then parcel will by default resend the file. You have the option of logging without restarting, restarting without logging, or doing both (-k). The command

    parcel -k xfer.log source host:dest

Will restart a transfer logged in xfer.log from directory source to directory dest on remote host.

Installation
------------

From the root parcel directory run the following:

    make
    sudo make install


Options
-------
The following options are currently supported by parcel:

		--help  print this message
		--remote (-r) host:dest  immitate scp with parcel and udpipe
		--checkpoint (-k) log_file  log transfer to file log_file.  If log_file exists
		 from previous transfer, resume transfer at last completed file.
		--verbose (-v)  verbose, mostly debug output
		--quiet  silence all warnings
		--encryption (-n)  enables encryption
		--mmap  memory map the file (involves extra memory copy)
		--full-root  do not trim file path but reconstruct full source path
		--fifo-test (-f)  will allow use of transferring from a fifo pipe to /dev/zero
		--log (-g) log_file  log transfer to file log_file but do not restart
		--restart log_file  restart transfer from file log_file but do not log

		-l [dest_dir]  listen for file transfer and write to dest_dir [default ./]
		-n enables encryption
		-v enables verbose output
		-b enables file logging
		This option will write log data to two files: debug-master.log and
		debug-minion.log. These will be written to the directory executed from
		\and the home directory on the remote end, respectively. They are appended
		o each subsequent run, so be sure to delete them if not necessary.
		-f enables fifo test
		This option will read from a named pipe as a fifo and automatically write
		to /dev/zero on the destination end. However, a path is still required for dest.

		Remote transfers: --remote (-r) host:dest
		This option requires udpipe [up] to be in your path. parcel
		will attempt to execute udpipe on the specified host over
		ssh.  If successful, it will transfer file list to
		directory dest.
