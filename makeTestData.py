#!/usr/bin/python

import os, sys, random, stat

MAX_INT_URAND = 4294967296

dataSizes = [ "small", "medium", "large", "huge" ]

smallTestParams = {
    'subfolders' : 2,
    'minNumFiles' : 8,
    'maxNumFiles' : 16,
    'minFileSize' : 512000,
    'maxFileSize' : 10485760,
}

medTestParams = {
    'subfolders' : 2,
    'minNumFiles' : 8,
    'maxNumFiles' : 16,
    'minFileSize' : 128000000,
    'maxFileSize' : 512857600,
}

largeTestParams = {
    'subfolders' : 2,
    'minNumFiles' : 8,
    'maxNumFiles' : 16,
    'minFileSize' : 1073741824,
    'maxFileSize' : 5368709120,
}

hugeTestParams = {
    'subfolders' : 0,
    'minNumFiles' : 1,
    'maxNumFiles' : 1,
    'minFileSize' : 5737418240,
    'maxFileSize' : 5737418240,
#    'minFileSize' : 10737418240,
#    'maxFileSize' : 53687091200,
}

dataParams = {
    "small": smallTestParams,
    "medium": medTestParams,
    "large": largeTestParams,
    "huge": hugeTestParams
}


def PrintHelp():
    cmdLineParts = sys.argv[0].split('/')
    for elements in cmdLineParts:
        if elements.find(".py"):
            scriptName = elements

    print ""
    print "** %s **" % scriptName
    print "A program to generate data files"
    print ""
    print "usage: %s [options]" % scriptName
    print "Options:"
    print "  --size=[small/medium/large/huge] size of data to generate (defaults to small)"
    print "  --datafile=[FILENAME] filename with data build params"
    print "  --dir=[DIRNAME] directory to write files to (defaults to 'test/data')"
    print "  --help - this help"

    return

def DebugPrint(string, verbose):
    if verbose:
        print string
    return


def ParseCommandLineArgs():

    commandLineArgs = {}
    commandLineArgs['dir'] = "test/data"
    commandLineArgs['size'] = "small"
    commandLineArgs['datafile'] = ""

    if len(sys.argv) >= 2:
        for arg in sys.argv:
            if not arg.find("--"):
                if arg.find("help") > 0:
                    PrintHelp()
                elif arg.find("size") > 0:
                    fileparts = arg.split("=")
                    if ( len(fileparts) == 2):
                        if fileparts[1].strip().lower() in dataSizes:
                            commandLineArgs['size'] = fileparts[1].strip().lower()
                elif arg.find("dir") > 0:
                    fileparts = arg.split("=")
                    if ( len(fileparts) == 2):
                        commandLineArgs['dir'] = fileparts[1]
                elif arg.find("datafile") > 0:
                    fileparts = arg.split("=")
                    if ( len(fileparts) == 2):
                        commandLineArgs['datafile'] = fileparts[1]
                elif arg.find("Belgium") > 0:
                    print "** Watch your language! **"

#    print commandLineArgs
    return commandLineArgs


def generateTestFile(location, filename, filesize):
    fullfilename = "%s/%s" % (location, filename)
    print "Creating file: %s" % fullfilename
    outFile = open(fullfilename, "wb+")
    while ( filesize > MAX_INT_URAND ):
        print "Writing %d bytes, %d remaining" % ( MAX_INT_URAND, (filesize - MAX_INT_URAND) )
        newFileByteArray = bytearray(os.urandom(MAX_INT_URAND))
        outFile.write(newFileByteArray)
        filesize = filesize - MAX_INT_URAND
    print "Finishing writing %d bytes" % ( filesize )
    newFileByteArray = bytearray(os.urandom(filesize))
    outFile.write(newFileByteArray)
    outFile.close()
    os.chmod(fullfilename, stat.S_IRUSR | stat.S_IWUSR | stat.S_IRGRP | stat.S_IWGRP | stat.S_IROTH | stat.S_IWOTH)

#def generateTestData(location, numsubfolders, minnumfiles, maxnumfiles, minfilesize, maxfilesize):
def generateTestData(location, params):
    targetdirs = [location]
    print "Generating new test data: "
    # make sure the target location exists
    if not os.path.exists(location):
        os.mkdir(location)
        os.chmod(location, stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO | stat.S_IWOTH)

    if params['subfolders'] > 0:
        for i in range(0, params['subfolders']):
            newsubfolder = "%s/parcel%03d" % (location, i)
            if not os.path.exists(newsubfolder):
                os.mkdir(newsubfolder)
                os.chmod(newsubfolder, stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO | stat.S_IWOTH)
            targetdirs.append(newsubfolder)

    numfiles = int(random.uniform(params['minNumFiles'], params['maxNumFiles']))

    for i in range(1, numfiles + 1):
        newfilename = "parcelTest%03d.dat" % i
        randomloc = targetdirs[int(random.uniform(0, len(targetdirs)))]
        filesize = int(random.uniform(params['minFileSize'], params['maxFileSize']))
#        sys.stderr.write("%d " % ((numfiles + 1) - i))
        generateTestFile(randomloc, newfilename, filesize)
#    sys.stderr.flush()
    print "complete"


def loadParamsData(filename):
    return largeTestParams

options = ParseCommandLineArgs()
if len(options['datafile']):
    curParams = loadParamsData(options['datafile'])
else:
    curParams = dataParams[options['size']]

generateTestData(options['dir'], curParams)
#generateTestData(options['dir'], LargeTestParams['NUM_TEST_SUBFOLDERS'], LargeTestParams['MIN_NUM_TEST_FILES'], LargeTestParams['MAX_NUM_TEST_FILES'], LargeTestParams['TEST_MINFILESIZE'], LargeTestParams['TEST_MAXFILESIZE'])
