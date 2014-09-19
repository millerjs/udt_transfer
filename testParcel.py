#!/usr/bin/python

import os, sys, shutil, getpass, difflib, subprocess, time
import socket
import fcntl
import struct
from distutils.spawn import find_executable

g_appName = "parcel"
TOTAL_WAITS = 3
SLEEP_TIME = 1

def PrintHelp():
    cmdLineParts = sys.argv[0].split('/')
    for elements in cmdLineParts:
        if elements.find(".py"):
            scriptName = elements

    print ""
    print "** %s **" % scriptName
    print "A program to test the functionality of parcel"
    print ""
    print "usage: %s [options]" % scriptName
    print "Options:"
    print "  --source=[DIRECTORY] folder to copy data from (mandatory to run)"
    print "  --target=[DIRECTORY] folder to copy data to (mandatory to run)"
    print "  --remotehost=[REMOTE_SYS] host to use as other end (defaults to localhost)"
    print "  --parceldir=[PATH] path to parcel exe on remote system (defaults to assuming installed in path, finds locally)"
    print "  --user=[username] user to use at other end (defaults to [whoami] on this system)"
    print "  --trips=[num] how many round trips to test (default 1)"
    print "  --crypto=[true/false] whether to use encryption (default false)"
    print "  --logging - use logging (logs files as debug_master.log and debug_minion.log)"
    print "  --verbose - verbose output"
    print "  --help - this help"

    return

def DebugPrint(string, verbose):
    if verbose:
        print string

    return

def ParseCommandLineArgs():

    commandLineArgs = {}
    commandLineArgs['user'] = getpass.getuser()
    commandLineArgs['remotehost'] = "localhost"
    commandLineArgs['trips'] = 1

    if len(sys.argv) >= 2:
        for arg in sys.argv:
            if not arg.find("--"):
                if arg.find("help") > 0:
                    PrintHelp()
                elif arg.find("verbose") > 0:
                    commandLineArgs['verbose'] = True
                elif arg.find("logging") > 0:
                    commandLineArgs['logging'] = True
                elif arg.find("source") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['source'] = fileparts[1].strip()
                elif arg.find("target") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['target'] = fileparts[1].strip()
                elif arg.find("trips") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['trips'] = int(fileparts[1].strip())
                elif arg.find("crypto") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['crypto'] = fileparts[1].strip().lower() == 'true'
                elif arg.find("remotehost") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['remotehost'] = fileparts[1].strip()
                elif arg.find("user") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['user'] = fileparts[1].strip()
                elif arg.find("parceldir") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['parceldir'] = fileparts[1].strip()
                elif arg.find("Belgium") > 0:
                    print "** Watch your language! **"

    print commandLineArgs
    return commandLineArgs


def DeleteDirectoryContents(directory, target = ""):

    if len(target):
        print "DeleteDirectoryContents: cleaning out %s %s" % ( directory, target )
        #ssh host 'rm -fr /your/file'
        commandStr = "ssh -o 'IdentitiesOnly yes' %s 'rm -rf %s/*'" % ( target, directory )
#        print commandStr
        os.system(commandStr)
    else:
        print "DeleteDirectoryContents: cleaning out %s" % directory
        for file in os.listdir(directory):
            filePath = os.path.join(directory, file)
            if os.path.isdir(filePath):
    #            print "dir: %s" % filePath
                shutil.rmtree(filePath)
            else:
    #            print "file: %s" % filePath
                os.unlink(filePath)


def CompareFiles(filename1, filename2):

    same = True

    size1 = os.path.getsize(filename1)
    size2 = os.path.getsize(filename2)
    if size1 != size2:
#        print "**ERROR** %s (%d) and %s (%d) not identical" % (filename1, size1, filename2, size2)
        same = False

    return same


def CompareDirectories(dir1, dir2, recurse = False):
    count = 0
    ok = True
    if recurse == False:
        print "\nCompareDirectories: comparing %s and %s" % (dir1, dir2)
    dirList1 = os.listdir(dir1)
    dirList1.sort()

    dirList2 = os.listdir(dir2)
    dirList2.sort()

    if recurse == False:
        sys.stdout.write("Checking: ")
#        sys.stdout.write("%s (%d) vs %s (%d) " % (dir1, len(dirList1), dir2, len(dirList2)))
        if ((len(dirList1) == 0) | (len(dirList2) == 0)):
            ok = False
    for file1, file2 in zip(dirList1, dirList2):
        fullFile1 = os.path.join(dir1, file1)
        fullFile2 = os.path.join(dir2, file2)

        if ( os.path.isdir(fullFile1) & os.path.isdir(fullFile2) ):
            count = count + CompareDirectories(fullFile1, fullFile2, True)
        else:
#            sys.stdout.write("Checking %15s vs %15s: " % (file1, file2))
            same = CompareFiles(fullFile1, fullFile2)
            if same:
                sys.stdout.write("+ ")
            else:
                ok = False
                sys.stdout.write("! ")

    if recurse == False:
        print

    return ok


def GetProcessIDs(targetProcess):

    processes = []

    output = subprocess.check_output(["ps", "-aux"])
    lines = output.split("\n")
    for line in lines:
        if line.find(targetProcess) != -1:
#            print line
            psParts = line.split()
#            print psParts
            processes.append(psParts[1])

#    print processes
    return processes

def KillProcessIDs(idList):
    for id in idList:
        os.system(commandStr)


def WaitForProcessesToExit(totalWaits):
    waitCount = 0
    pids = GetProcessIDs(g_appName)
    myPid = str(os.getpid())
    while len(pids) > 0 :
        if (myPid in pids) & (len(pids) == 1):
            break
        if waitCount > totalWaits:
            print "parcel instances still running, we've waited long enough, killing"
            commandStr = "killall %s" % g_appName
            os.system(commandStr)
            waitCount = 0
        else:
            print "parcel instances still running, waiting"
            print pids
            time.sleep(SLEEP_TIME)
            waitCount = waitCount + 1
        pids = GetProcessIDs(g_appName)


def CallParcel(parcelArgs, remoteStr, sourceStr):
    commandStr = "./%s %s %s %s" % ( g_appName, parcelArgs, sourceStr, remoteStr)
    print "********"
    print commandStr
    print "********"
    os.system(commandStr)
    WaitForProcessesToExit(TOTAL_WAITS)

def PrintResults(results):
    i = 0
    print
    print "Total Results"
    print "============="

    while i < len(results):
        if ( results[i] ):
            lrStatus = "ok"
        else:
            lrStatus = "failed"

        print "Pass %d:  %s" % ( i, lrStatus )
        i = i + 1

def GetIPAddress(ifname):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    return socket.inet_ntoa(fcntl.ioctl(
        s.fileno(),
        0x8915,  # SIOCGIFADDR
        struct.pack('256s', ifname[:15])
    )[20:24])

#get_ip_address('eth0')  # '192.168.0.110'


def SetupParcelArgs(cmdArgs):

    parcelArgs = ""
    # use verbose output if requested
#        if cmdArgs['verbose']:
    if 'verbose' in cmdArgs:
        parcelArgs += "-v "

    # use logging if requested
#        if cmdArgs['verbose']:
    if 'logging' in cmdArgs:
        parcelArgs += "-b "

    # use encryption if requested
#        if cmdArgs['crypto']:
    if 'crypto' in cmdArgs:
        parcelArgs += "-n "

    # set remote path to parcel app if given
    if 'parceldir' in cmdArgs:
        parcelArgs += "-c %s/%s" % (cmdArgs['parceldir'], g_appName)

    return parcelArgs.strip()


def FindAnotherRemoteDir(localDir):

    newLocalDir = ""

    # does the localDir end with a slash?
    if localDir[-1] == '/':
        newLocalDir = localDir[:-1]
    else:
        newLocalDir = localDir

    index = 1
    newLocalDir = "%s%s" % (localDir, str(index))
    while os.path.exists(newLocalDir):
        index = index + 1
        newLocalDir = "%s%s" % (localDir, str(index))

    os.makedirs(newLocalDir)

    return newLocalDir

#
#
# Main app
#
#

cmdArgs = ParseCommandLineArgs()

passResults = []

ids = GetProcessIDs(g_appName)
if len(ids) > 0 :
    print "parcel instances still running, killing"
    commandStr = "killall %s" % g_appName
    os.system(commandStr)

if ('source' in cmdArgs) & ('target' in cmdArgs):
    if (cmdArgs['source'] != cmdArgs['target']):

        parcelArgs = SetupParcelArgs(cmdArgs)

        remoteUser = cmdArgs['user']
        localUser = getpass.getuser()
        remoteSys = cmdArgs['remotehost']
        if ( remoteSys != "localhost" ):
            localSys = GetIPAddress('eth0')
        else:
            localSys = GetIPAddress('lo')

        localDir = cmdArgs['source']
        remoteDir = cmdArgs['target']

        newLocalDir = FindAnotherRemoteDir(localDir)

        remoteStr = "%s@%s:%s" % ( remoteUser, remoteSys, remoteDir )
        backStr = "%s@%s:%s" % ( localUser, localSys, newLocalDir)
        remoteDir = os.path.join(os.path.expanduser("~"), remoteDir)
        passes = cmdArgs['trips']
        while passes > 0:
            # clear out the directories
            DeleteDirectoryContents(newLocalDir)
            DeleteDirectoryContents(remoteDir, "%s@%s" % (remoteUser, remoteSys))
            # do a round trip
            # send from local to remote
            CallParcel(parcelArgs, remoteStr, localDir)
            # get from remote to local
            CallParcel(parcelArgs, newLocalDir, remoteStr)
#            LocalToRemote(cmdArgs, parcelArgs, remoteStr, localDir)
#            RemoteToLocal(cmdArgs, parcelArgs, remoteStr, remoteDir, newLocalDir)

            # compare the files after the trip
            result = CompareDirectories(localDir, newLocalDir)

            passResults.append(result)
            passes = passes - 1

        PrintResults(passResults)
        # delete contents on way out
        DeleteDirectoryContents(newLocalDir)
        DeleteDirectoryContents(remoteDir, "%s@%s" % (remoteUser, remoteSys))
        os.rmdir(newLocalDir)
    else:
        print "Source and target cannot be the same"
else:
    PrintHelp()
