#!/usr/bin/python

import os, sys, random, stat

MAX_INT_URAND = 4294967296

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

def generateTestData(location, numsubfolders, minnumfiles, maxnumfiles, minfilesize, maxfilesize):
    targetdirs = [location]
    print "Generating new test data: "
    # make sure the target location exists
    if not os.path.exists(location):
        os.mkdir(location)
        os.chmod(location, stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO | stat.S_IWOTH)

    if numsubfolders > 0:
        for i in range(0, numsubfolders):
            newsubfolder = "%s/parcel%03d" % (location, i)
            if not os.path.exists(newsubfolder):
                os.mkdir(newsubfolder)
                os.chmod(newsubfolder, stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO | stat.S_IWOTH)
            targetdirs.append(newsubfolder)

    numfiles = int(random.uniform(minnumfiles, maxnumfiles))

    for i in range(1, numfiles + 1):
        newfilename = "parcelTest%03d.dat" % i
        randomloc = targetdirs[int(random.uniform(0, len(targetdirs)))]
        filesize = int(random.uniform(minfilesize, maxfilesize))
#        sys.stderr.write("%d " % ((numfiles + 1) - i))
        generateTestFile(randomloc, newfilename, filesize)
#    sys.stderr.flush()
    print "complete"

LargeTestParams = {
    'NUM_TEST_SUBFOLDERS' : 2,
    'MIN_NUM_TEST_FILES' : 4,
    'MAX_NUM_TEST_FILES' : 10,
    'TEST_MINFILESIZE' : 10737418240,
    'TEST_MAXFILESIZE' : 53687091200,
#    'TEST_MINFILESIZE' : 1073741,
#    'TEST_MAXFILESIZE' : 5368709,
}

if len(sys.argv) < 2:
    outDir = "test"
else:
    outDir = sys.argv[1]

generateTestData(outDir, LargeTestParams['NUM_TEST_SUBFOLDERS'], LargeTestParams['MIN_NUM_TEST_FILES'], LargeTestParams['MAX_NUM_TEST_FILES'], LargeTestParams['TEST_MINFILESIZE'], LargeTestParams['TEST_MAXFILESIZE'])
