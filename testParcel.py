#!/usr/bin/python

import os, sys, shutil, getpass, difflib, subprocess, time
import socket, fcntl, struct, random
import subprocess
from distutils.spawn import find_executable
import unittest

g_appName = "parcel"
TOTAL_WAITS = 3
SLEEP_TIME = 1

class ParcelTestFunctions(unittest.TestCase):
    def setUp(self):
        print "setUp: start"

    def tearDown(self):
        print "tearDown: start"






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
    print "  --gendata - generate the data files (just once)"
    print "  --genloop - generate the data files every trip"
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
    commandLineArgs['gendata'] = False
    commandLineArgs['genloop'] = False

    if len(sys.argv) >= 2:
        for arg in sys.argv:
            if not arg.find("--"):
                if arg.find("help") > 0:
                    PrintHelp()
                elif arg.find("verbose") > 0:
                    commandLineArgs['verbose'] = True
                elif arg.find("logging") > 0:
                    commandLineArgs['logging'] = True
                elif arg.find("gendata") > 0:
                    commandLineArgs['gendata'] = True
                elif arg.find("genloop") > 0:
                    commandLineArgs['genloop'] = True
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

#    print commandLineArgs
    return commandLineArgs


def DeleteDirectoryContents(directory, target = ""):

    if len(target):
#        print "DeleteDirectoryContents: cleaning out %s %s" % ( directory, target )
        #ssh host 'rm -fr /your/file'
        commandStr = "ssh -A -o 'IdentitiesOnly yes' %s 'rm -rf %s/*'" % ( target, directory )
#        print commandStr
        os.system(commandStr)
    else:
#        print "DeleteDirectoryContents: cleaning out %s" % directory
        if os.path.exists(directory):
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

def GenerateTestFile(location, filename, filesize):
    fullfilename = "%s/%s" % (location, filename)
#    print "Creating file %s in %s" % (fullfilename, os.getcwd())
    outFile = open(fullfilename, "wb+")
    newFileByteArray = bytearray(os.urandom(filesize))
    outFile.write(newFileByteArray)
    outFile.close()

def GenerateTestData(location, numsubfolders, numfiles, minfilesize, maxfilesize):

    targetdirs = [location]

    # make sure the target location exists
    if not os.path.exists(location):
        os.mkdir(location)

    if numsubfolders > 0:
        for i in range(0, numsubfolders):
            newsubfolder = "%s/parcel%03d" % (location, i)
            if not os.path.exists(newsubfolder):
#                print "Making subfolder: %s" % newsubfolder
                os.mkdir(newsubfolder)
            targetdirs.append(newsubfolder)

    print targetdirs
    for i in range(1, numfiles + 1):
        newfilename = "parcelTest%03d.dat" % i
        randomloc = targetdirs[int(random.uniform(0, len(targetdirs)))]
        filesize = int(random.uniform(minfilesize, maxfilesize))
#        print "Generating file %s in %s of size %d" % (newfilename, randomloc, filesize)
        GenerateTestFile(randomloc, newfilename, filesize)

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
#    print commandStr
    os.system(commandStr)
    WaitForProcessesToExit(TOTAL_WAITS)

def PrintResultsBrief(results):

    sys.stdout.write("Results: ")
    for item in results:
        if ( item ):
            sys.stdout.write("+")
        else:
            sys.stdout.write("-")
    print

def PrintResults(results):
    i = 0
    okCount = 0
    failCount = 0
    print
    print "Total Results"
    print "============="

    while i < len(results):
        if ( results[i] ):
            lrStatus = "ok"
            okCount = okCount + 1
        else:
            lrStatus = "failed"
            failCount = failCount + 1

        print "Pass %d:  %s" % ( i + 1, lrStatus )
        i = i + 1
    percentTotal = ((float(okCount) / float(len(results))) * float(100))
    print "\nRESULTS"
    print "======="
    print "%d trips, %d passed, %d failed, %.02f %% success\n" % (len(results), okCount, failCount, percentTotal)


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
    if 'verbose' in cmdArgs:
        parcelArgs += "-v "

    # use logging if requested
    if 'logging' in cmdArgs:
        parcelArgs += "-b "

    # use encryption if requested
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
def Main():
    cmdArgs = ParseCommandLineArgs()

    passResults = []

    ids = GetProcessIDs(g_appName)
#    WaitForProcessesToExit(0)
#    if len(ids) > 0 :
#        print "parcel instances still running, killing"
#        commandStr = "killall %s" % g_appName
#        os.system(commandStr)

    if ('source' in cmdArgs) & ('target' in cmdArgs):
        if (cmdArgs['source'] != cmdArgs['target']):

            parcelArgs = SetupParcelArgs(cmdArgs)

            remoteUser = cmdArgs['user']
            gendata = cmdArgs['gendata']
            genloop = cmdArgs['genloop']
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
                # kill any processes hanging around
                WaitForProcessesToExit(0)

                print
                print "****** Trip %02d ******" % ((cmdArgs['trips'] - passes) + 1)

                if gendata || genloop:
                    DeleteDirectoryContents(localDir)
                    # create the data
                    GenerateTestData(localDir, 3, 32, 1048576, 52428800)
                    gendata = False

                # clear out the other directories
                DeleteDirectoryContents(newLocalDir)
                DeleteDirectoryContents(remoteDir, "%s@%s" % (remoteUser, remoteSys))


                # do a round trip
                # send from local to remote
                print "Local to remote..."
                CallParcel(parcelArgs, remoteStr, localDir)
                # get from remote to local
#                print "Let's wait a few..."
                time.sleep(1)

                print "Remote to local..."
                CallParcel(parcelArgs, newLocalDir, remoteStr)
    #            LocalToRemote(cmdArgs, parcelArgs, remoteStr, localDir)
    #            RemoteToLocal(cmdArgs, parcelArgs, remoteStr, remoteDir, newLocalDir)
#                print "Let's wait a few..."
                time.sleep(1)

                # compare the files after the trip
                result = CompareDirectories(localDir, newLocalDir)

                passResults.append(result)
                print
                PrintResultsBrief(passResults)
                print "****** Trip done ******"
                print
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

Main()
