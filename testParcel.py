#!/usr/bin/python

import os, sys, shutil, getpass, difflib, subprocess, time

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
    print "  --help - this help"
    print "  --verbose - verbose output"

    return

def DebugPrint(string, verbose):
    if verbose:
        print string

    return

def ParseCommandLineArgs():

    commandLineArgs = {}
    commandLineArgs['verbose'] = False
    commandLineArgs['user'] = getpass.getuser()
    commandLineArgs['remoteSys'] = "localhost"
    commandLineArgs['parcelDir'] = os.getcwd()

    if len(sys.argv) >= 2:
        for arg in sys.argv:
            if not arg.find("--"):
                if arg.find("help") > 0:
                    PrintHelp()
                elif arg.find("verbose") > 0:
                    commandLineArgs['verbose'] = True
                elif arg.find("source") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['source'] = fileparts[1].strip()
                elif arg.find("target") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['target'] = fileparts[1].strip()
                elif arg.find("remote") > 0:
                    fileparts = arg.split("=")
                    if len(fileparts) > 1:
                        commandLineArgs['target'] = fileparts[1].strip()
                elif arg.find("Belgium") > 0:
                    print "** Watch your language! **"

    print commandLineArgs
    return commandLineArgs


def DeleteDirectoryContents(directory):

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


def CompareDirectories(dir1, dir2):
    ok = True
    print "CompareDirectories: comparing %s and %s" % (dir1, dir2)
    dirList1 = os.listdir(dir1)
    dirList1.sort()

    dirList2 = os.listdir(dir2)
    dirList2.sort()

    for file1, file2 in zip(dirList1, dirList2):
        fullFile1 = os.path.join(dir1, file1)
        fullFile2 = os.path.join(dir2, file2)

        if ( os.path.isdir(fullFile1) & os.path.isdir(fullFile2) ):
            CompareDirectories(fullFile1, fullFile2)
        else:
            sys.stdout.write("Checking %15s vs %15s: " % (file1, file2))
            same = CompareFiles(fullFile1, fullFile2)
            if same:
                print "ok"
            else:
                ok = False
                print "different"

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


appName = "parcel"

cmdArgs = ParseCommandLineArgs()
TOTAL_WAITS = 3
SLEEP_TIME = 1

ids = GetProcessIDs(appName)
if len(ids) > 0 :
    print "parcel instances still running, killing"
    commandStr = "killall %s" % appName
    os.system(commandStr)

if ('source' in cmdArgs) & ('target' in cmdArgs):
    if (cmdArgs['source'] != cmdArgs['target']):
        remoteDir = os.path.join(os.path.expanduser("~"), cmdArgs['target'])

        # clear out the dest directory
        DeleteDirectoryContents(remoteDir)

        # /parcel -v -c /home/flynn/Projects/Parcel/parcel test/data flynn@localhost:out2
        # call parcel local to remote
        parcelArgs = "-v -c %s/%s" % (cmdArgs['parcelDir'], appName)
        remoteStr = "%s@%s:%s" % ( cmdArgs['user'], cmdArgs['remoteSys'], cmdArgs['target'] )
        commandStr = "./%s %s %s %s" % ( appName, parcelArgs, cmdArgs['source'], remoteStr)
        print "********"
        print commandStr
        print "********"
        os.system(commandStr)
        #output = subprocess.check_output(commandStr, shell=True)
        waitCount = 0
        while len(GetProcessIDs(appName)) > 0 :
            if waitCount > TOTAL_WAITS:
                print "parcel instances still running, we've waited long enough, killing"
                commandStr = "killall %s" % appName
                os.system(commandStr)
            else:
                print "parcel instances still running, waiting"
                time.sleep(SLEEP_TIME)
                waitCount = waitCount + 1

        # verify the directories
        if CompareDirectories(cmdArgs['source'], remoteDir):

            # clear out the source directory
            DeleteDirectoryContents(cmdArgs['source'])

            # see if anything is still running
            ids = GetProcessIDs(appName)
            if len(ids) > 0 :
                print "parcel instances still running, killing"
                commandStr = "killall %s" % appName
                os.system(commandStr)

            # /parcel -v -c /home/flynn/Projects/Parcel/parcel flynn@localhost:out2 test/data
            # call parcel remote to local
            commandStr = "./parcel %s %s %s" % ( parcelArgs, remoteStr, cmdArgs['source'])
            print "********"
            print commandStr
            print "********"
            os.system(commandStr)

            # verify the directories
            CompareDirectories(cmdArgs['source'], remoteDir)

            ids = GetProcessIDs(appName)
            if len(ids) > 0 :
                print "parcel instances still running, killing"
                commandStr = "killall %s" % appName
                os.system(commandStr)

        else:
            print "Compare error, quitting"

    else:
        print "Source and target cannot be the same"
else:
    PrintHelp()
