#!/usr/bin/python

import os, sys, shutil, getpass, difflib, subprocess, time
import socket, fcntl, struct, random
import subprocess, filecmp
from distutils.spawn import find_executable
import unittest

g_appName = "parcel"
TOTAL_WAITS = 3
SLEEP_TIME = 1

NUM_TEST_SUBFOLDERS = 2
NUM_TEST_FILES = 8
TEST_MINFILESIZE = 512000
TEST_MAXFILESIZE = 1048576

class ParcelTestFunctions(unittest.TestCase):

    passData = {}
    parcelArgs = {}

#
# unittest needed routines
#

    def setUp(self):
        cmdArgs = {}
        testName = self.shortDescription()

        if testName == "encryptedLocalRoundTrip":
            print "*** setUp: start %s" % testName
            cmdArgs['crypto'] = True
            cmdArgs['parceldir'] = "/home/flynn/Projects/Parcel"
            self.parcelArgs = self.setupParcelArgs(cmdArgs)
            self.passData['remoteSys'] = "localhost"
            self.passData['localDir'] = "test/data_test"
            self.passData['remoteDir'] = "test/out1"

        elif testName == "unencryptedLocalRoundTrip":
            print "*** setUp: start %s" % testName
            cmdArgs['crypto'] = False
            cmdArgs['parceldir'] = "/home/flynn/Projects/Parcel"
            self.parcelArgs = self.setupParcelArgs(cmdArgs)
            self.passData['remoteSys'] = "localhost"
            self.passData['localDir'] = "test/data_test"
            self.passData['remoteDir'] = "test/out1"

        elif testName == "encryptedRemoteRoundTrip":
            print "*** setUp: start %s" % testName
            cmdArgs['crypto'] = True
            cmdArgs['parceldir'] = "/home/flynn/Projects/Parcel"
            self.parcelArgs = self.setupParcelArgs(cmdArgs)
            self.passData['remoteSys'] = "ritchie"
            self.passData['localDir'] = "test/data_test"
            self.passData['remoteDir'] = "test/out1"

        elif testName == "unencryptedRemoteRoundTrip":
            print "*** setUp: start %s" % testName
            cmdArgs['crypto'] = False
            cmdArgs['parceldir'] = "/home/flynn/Projects/Parcel"
            self.parcelArgs = self.setupParcelArgs(cmdArgs)
            self.passData['remoteSys'] = "ritchie"
            self.passData['localDir'] = "test/data_test"
            self.passData['remoteDir'] = "test/out1"

        self.passData['remoteUser'] = "ubuntu"
        self.passData['gendata'] = True
        self.passData['genloop'] = False
        self.passData['localUser'] = getpass.getuser()
        if ( self.passData['remoteSys'] != "localhost" ):
            self.passData['localSys'] = self.getIPAddress('eth0')
        else:
            self.passData['localSys'] = self.getIPAddress('lo')
            self.passData['remoteUser'] = self.passData['localUser']

        self.passData['newLocalDir'] = self.findAnotherRemoteDir(self.passData['localDir'])
        self.passData['remoteStr'] = "%s@%s:%s" % ( self.passData['remoteUser'], self.passData['remoteSys'], self.passData['remoteDir'] )
        self.passData['backStr'] = "%s@%s:%s" % ( self.passData['localUser'], self.passData['localSys'], self.passData['newLocalDir'])
        self.passData['remoteDir'] = os.path.join(os.path.expanduser("~"), self.passData['remoteDir'])

        if (self.passData['gendata'] == True):
            self.deleteDirectoryContents(self.passData['localDir'])
            # create the data
            self.generateTestData(self.passData['localDir'], NUM_TEST_SUBFOLDERS, NUM_TEST_FILES, TEST_MINFILESIZE, TEST_MAXFILESIZE)


    def tearDown(self):
        print "tearDown: start"

    def testEncryptedLocalRoundTrip(self):
        """encryptedLocalRoundTrip"""
        self.roundTrip()

    def testUnencryptedLocalRoundTrip(self):
        """unencryptedLocalRoundTrip"""
        self.roundTrip()

#    def testEncryptedRemoteRoundTrip(self):
#        """encryptedRemoteRoundTrip"""
#        self.roundTrip()

#    def testUnencryptedRemoteRoundTrip(self):
#        """unencryptedRemoteRoundTrip"""
#        self.roundTrip()


#
# implementation specific routines
#

    def setupParcelArgs(self, cmdArgs):
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

    def deleteDirectoryContents(self, directory, target = ""):
        if len(target):
            commandStr = "ssh -A -o 'IdentitiesOnly yes' %s 'rm -rf %s/*'" % ( target, directory )
            os.system(commandStr)
        else:
            if os.path.exists(directory):
                for file in os.listdir(directory):
                    filePath = os.path.join(directory, file)
                    if os.path.isdir(filePath):
                        shutil.rmtree(filePath)
                    else:
                        os.unlink(filePath)

    def compareFiles(self, filename1, filename2):
        same = True
        size1 = os.path.getsize(filename1)
        size2 = os.path.getsize(filename2)
        if size1 != size2:
            same = False
        else:
            same = filecmp.cmp(filename1, filename2)

        return same

    def compareDirectories(self, dir1, dir2, recurse = False):
        count = 0
        ok = True
#        if recurse == False:
#            print "\nCompareDirectories: comparing %s and %s" % (dir1, dir2)
        dirList1 = os.listdir(dir1)
        dirList1.sort()

        dirList2 = os.listdir(dir2)
        dirList2.sort()

        if recurse == False:
#            sys.stderr.write("Checking: ")
            if ((len(dirList1) == 0) | (len(dirList2) == 0)):
                ok = False
        for file1, file2 in zip(dirList1, dirList2):
            fullFile1 = os.path.join(dir1, file1)
            fullFile2 = os.path.join(dir2, file2)

            if ( os.path.isdir(fullFile1) & os.path.isdir(fullFile2) ):
                count = count + self.compareDirectories(fullFile1, fullFile2, True)
            else:
                same = self.compareFiles(fullFile1, fullFile2)
                if same != True:
                    ok = False
#                    sys.stderr.write("! ")
#                else:
#                    sys.stderr.write("+ ")

        if recurse == False:
            print

        return ok

    def generateTestFile(self, location, filename, filesize):
        fullfilename = "%s/%s" % (location, filename)
        outFile = open(fullfilename, "wb+")
        newFileByteArray = bytearray(os.urandom(filesize))
        outFile.write(newFileByteArray)
        outFile.close()

    def generateTestData(self, location, numsubfolders, numfiles, minfilesize, maxfilesize):
        targetdirs = [location]
        sys.stderr.write("Generating new test data")
        # make sure the target location exists
        if not os.path.exists(location):
            os.mkdir(location)

        if numsubfolders > 0:
            for i in range(0, numsubfolders):
                newsubfolder = "%s/parcel%03d" % (location, i)
                if not os.path.exists(newsubfolder):
                    os.mkdir(newsubfolder)
                targetdirs.append(newsubfolder)

        for i in range(1, numfiles + 1):
            newfilename = "parcelTest%03d.dat" % i
            randomloc = targetdirs[int(random.uniform(0, len(targetdirs)))]
            filesize = int(random.uniform(minfilesize, maxfilesize))
            sys.stderr.write(".")
            self.generateTestFile(randomloc, newfilename, filesize)
        print "complete"

    def getProcessIDs(self, targetProcess):
        processes = []
        output = subprocess.check_output(["ps", "-aux"])
        lines = output.split("\n")
        for line in lines:
            if line.find(targetProcess) != -1:
                psParts = line.split()
                processes.append(psParts[1])

        return processes

    def killProcessIDs(self, idList):
        for id in idList:
            os.system(commandStr)

    def waitForProcessesToExit(self, totalWaits):
        waitCount = 0
        pids = self.getProcessIDs(g_appName)
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
            pids = self.getProcessIDs(g_appName)

    def printResultsBrief(self, results):
        sys.stderr.write("Results: ")
        for item in results:
            if ( item ):
                sys.stderr.write("+")
            else:
                sys.stderr.write("-")
        print

    def printResults(self, results):
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

    def getIPAddress(self, ifname):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        return socket.inet_ntoa(fcntl.ioctl(
            s.fileno(),
            0x8915,  # SIOCGIFADDR
            struct.pack('256s', ifname[:15])
        )[20:24])

    def findAnotherRemoteDir(self, localDir):
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

    def callParcel(self, parcelArgs, remoteStr, sourceStr):
        commandStr = "./%s %s %s %s" % ( g_appName, parcelArgs, sourceStr, remoteStr)
        os.system(commandStr)
        self.waitForProcessesToExit(TOTAL_WAITS)

    def roundTrip(self):
        if self.passData['genloop'] == True:
            self.deleteDirectoryContents(self.passData['localDir'])
            # create the data
            self.generateTestData(self.passData['localDir'], NUM_TEST_SUBFOLDERS, NUM_TEST_FILES, TEST_MINFILESIZE, TEST_MAXFILESIZE)

        # clear out the other directories
        self.deleteDirectoryContents(self.passData['newLocalDir'])
        self.deleteDirectoryContents(self.passData['remoteDir'], "%s@%s" % (self.passData['remoteUser'], self.passData['remoteSys']))

        # do a round trip
        # send from local to remote
        print "Local to remote..."
        self.callParcel(self.parcelArgs, self.passData['remoteStr'], self.passData['localDir'])
        # get from remote to local
        time.sleep(1)

        print "Remote to local..."
        self.callParcel(self.parcelArgs, self.passData['newLocalDir'], self.passData['remoteStr'])
        time.sleep(1)

        # compare the files after the trip
        result = self.compareDirectories(self.passData['localDir'], self.passData['newLocalDir'])
        print "Test result = %u" % result

        self.assertTrue(result, "ERROR: directories not equal.")

        return result

if __name__ == '__main__':
    unittest.main()
