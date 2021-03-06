
/******************************************************************************
 *
 *  This file is part of canu, a software program that assembles whole-genome
 *  sequencing reads into contigs.
 *
 *  This software is based on:
 *    'Celera Assembler' (http://wgs-assembler.sourceforge.net)
 *    the 'kmer package' (http://kmer.sourceforge.net)
 *  both originally distributed by Applera Corporation under the GNU General
 *  Public License, version 2.
 *
 *  Canu branched from Celera Assembler at its revision 4587.
 *  Canu branched from the kmer project at its revision 1994.
 *
 *  This file is derived from:
 *
 *    src/AS_CNS/utgcns.C
 *
 *  Modifications by:
 *
 *    Brian P. Walenz from 2009-OCT-05 to 2014-MAR-31
 *      are Copyright 2009-2014 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Brian P. Walenz beginning on 2014-DEC-30
 *      are Copyright 2014-2015 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

const char *mainid = "$Id$";

#include "AS_global.H"
#include "gkStore.H"
#include "tgStore.H"
#include "abAbacus.H"

#include "AS_UTL_decodeRange.H"

#include "stashContains.H"

#include "unitigConsensus.H"

#include <map>
#include <algorithm>



int
main (int argc, char **argv) {
  char  *gkpName = NULL;

  char   *tigName = NULL;
  uint32  tigVers = UINT32_MAX;
  uint32  tigPart = UINT32_MAX;

  char   *tigFileName = NULL;

  uint32  utgBgn  = UINT32_MAX;
  uint32  utgEnd  = UINT32_MAX;

  char *outResultsName = NULL;
  char *outLayoutsName = NULL;
  char *outSeqName     = NULL;

  FILE *outResultsFile = NULL;
  FILE *outLayoutsFile = NULL;
  FILE *outSeqFile     = NULL;

  bool   forceCompute = false;

  double errorRate    = 0.06;
  double errorRateMax = 0.40;
  uint32 minOverlap   = 40;

  int32  numFailures = 0;

  bool   showResult = false;

  double maxCov = 0.0;
  uint32 maxLen = UINT32_MAX;

  uint32 verbosity = 0;

  argc = AS_configure(argc, argv);

  int arg=1;
  int err=0;
  while (arg < argc) {
    if        (strcmp(argv[arg], "-G") == 0) {
      gkpName = argv[++arg];

    } else if (strcmp(argv[arg], "-T") == 0) {
      tigName = argv[++arg];
      tigVers = atoi(argv[++arg]);
      tigPart = atoi(argv[++arg]);

      if (argv[arg][0] == '.')
        tigPart = UINT32_MAX;

      if (tigVers == 0)
        fprintf(stderr, "invalid tigStore version (-T store version partition) '-t %s %s %s'.\n", argv[arg-2], argv[arg-1], argv[arg]), exit(1);
      if (tigPart == 0)
        fprintf(stderr, "invalid tigStore partition (-T store version partition) '-t %s %s %s'.\n", argv[arg-2], argv[arg-1], argv[arg]), exit(1);

    } else if (strcmp(argv[arg], "-u") == 0) {
      AS_UTL_decodeRange(argv[++arg], utgBgn, utgEnd);

    } else if (strcmp(argv[arg], "-t") == 0) {
      tigFileName = argv[++arg];

    } else if (strcmp(argv[arg], "-O") == 0) {
      outResultsName = argv[++arg];

    } else if (strcmp(argv[arg], "-L") == 0) {
      outLayoutsName = argv[++arg];

    } else if (strcmp(argv[arg], "-F") == 0) {
      outSeqName = argv[++arg];

    } else if (strcmp(argv[arg], "-e") == 0) {
      errorRate = atof(argv[++arg]);

    } else if (strcmp(argv[arg], "-em") == 0) {
      errorRateMax = atof(argv[++arg]);

    } else if (strcmp(argv[arg], "-l") == 0) {
      minOverlap = atoi(argv[++arg]);

    } else if (strcmp(argv[arg], "-f") == 0) {
      forceCompute = true;

    } else if (strcmp(argv[arg], "-v") == 0) {
      showResult = true;

    } else if (strcmp(argv[arg], "-V") == 0) {
      verbosity++;

    } else if (strcmp(argv[arg], "-maxcoverage") == 0) {
      maxCov   = atof(argv[++arg]);

    } else if (strcmp(argv[arg], "-maxlength") == 0) {
      maxLen   = atof(argv[++arg]);

    } else {
      fprintf(stderr, "%s: Unknown option '%s'\n", argv[0], argv[arg]);
      err++;
    }

    arg++;
  }

  if (gkpName == NULL)
    err++;

  if ((tigFileName == NULL) && (tigName == NULL))
    err++;

  if (err) {
    fprintf(stderr, "usage: %s [opts]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "  INPUT\n");
    fprintf(stderr, "    -G g            Load reads from gkStore 'g'\n");
    fprintf(stderr, "    -T t v p        Load unitigs from tgStore 't', version 'v', partition 'p'.\n");
    fprintf(stderr, "                      Expects reads will be in gkStore partition 'p' as well\n");
    fprintf(stderr, "                      Use p='.' to specify no partition\n");
    fprintf(stderr, "    -t file         Test the computation of the unitig layout in 'file'\n");
    fprintf(stderr, "                      'file' can be from:\n");
    fprintf(stderr, "                        'tgStoreDump -d layout' (human readable layout format)\n");
    fprintf(stderr, "                        'utgcns -L'             (human readable layout format)\n");
    fprintf(stderr, "                        'utgcns -O'             (binary multialignment format)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  OUTPUT\n");
    fprintf(stderr, "    -O results      Write computed tigs to binary output file 'results'\n");
    fprintf(stderr, "    -L layouts      Write computed tigs to layout output file 'layouts'\n");
    fprintf(stderr, "    -F fastq        Write computed tigs to fastq  output file 'fastq'\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  TIG SELECTION (if -T input is used)\n");
    fprintf(stderr, "    -u b            Compute only unitig ID 'b' (must be in the correct partition!)\n");
    fprintf(stderr, "    -u b-e          Compute only unitigs from ID 'b' to ID 'e'\n");
    fprintf(stderr, "    -f              Recompute unitigs that already have a multialignment\n");
    fprintf(stderr, "    -maxlength l    Do not compute consensus for unitigs longer than l bases.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  PARAMETERS\n");
    fprintf(stderr, "    -e e            Expect alignments at up to fraction e error\n");
    fprintf(stderr, "    -em m           Don't ever allow alignments more than fraction m error\n");
    fprintf(stderr, "    -l l            Expect alignments of at least l bases\n");
    fprintf(stderr, "    -maxcoverage c  Use non-contained reads and the longest contained reads, up to\n");
    fprintf(stderr, "                    C coverage, for consensus generation.  The default is 0, and will\n");
    fprintf(stderr, "                    use all reads.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  LOGGING\n");
    fprintf(stderr, "    -v              Show multialigns.\n");
    fprintf(stderr, "    -V              Enable debugging option 'verbosemultialign'.\n");
    fprintf(stderr, "\n");


    if (gkpName == NULL)
      fprintf(stderr, "ERROR:  No gkpStore (-G) supplied.\n");

    if ((tigFileName == NULL) && (tigName == NULL))
      fprintf(stderr, "ERROR:  No tigStore (-T) OR no test unitig (-t) supplied.\n");

    exit(1);
  }

  errno = 0;

  //  Open output files

  if (outResultsName)
    outResultsFile = fopen(outResultsName, "w");
  if (errno)
    fprintf(stderr, "Failed to open output results file '%s': %s\n", outResultsName, strerror(errno)), exit(1);

  if (outLayoutsName)
    outLayoutsFile = fopen(outLayoutsName, "w");
  if (errno)
    fprintf(stderr, "Failed to open output layout file '%s': %s\n", outLayoutsName, strerror(errno)), exit(1);

  if (outSeqName)
    outSeqFile = fopen(outSeqName, "w");
  if (errno)
    fprintf(stderr, "Failed to open output FASTQ file '%s': %s\n", outSeqName, strerror(errno)), exit(1);

  //  Open gatekeeper for read only, and load the partitioned data if tigPart > 0.

  fprintf(stderr, "-- Opening gkpStore '%s' partition %u.\n", gkpName, tigPart);

  gkStore   *gkpStore = new gkStore(gkpName, gkStore_readOnly, tigPart);
  tgStore   *tigStore = NULL;
  FILE      *tigFile  = NULL;

  if (tigName) {
    fprintf(stderr, "-- Opening tigStore '%s' version %u.\n", tigName, tigVers);
    tigStore = new tgStore(tigName, tigVers);
  }

  if (tigFileName) {
    fprintf(stderr, "-- Opening tigFile '%s'.\n", tigFileName);

    errno = 0;
    tigFile = fopen(tigFileName, "r");
    if (errno)
      fprintf(stderr, "Failed to open input tig file '%s': %s\n", tigFileName, strerror(errno)), exit(1);
  }

  //  Decide on what to compute.  Either all unitigs, or a single unitig, or a special case test.

  uint32  b = 0;
  uint32  e = UINT32_MAX;

  if (tigStore) {
    if (utgEnd > tigStore->numTigs() - 1)
      utgEnd = tigStore->numTigs() - 1;

    if (utgBgn != UINT32_MAX) {
      b = utgBgn;
      e = utgEnd;

    } else {
      b = 0;
      e = utgEnd;
    }

    fprintf(stderr, "-- Computing unitig consensus for b="F_U32" to e="F_U32" with errorRate %0.4f (max %0.4f) and minimum overlap "F_U32"\n",
            b, e, errorRate, errorRateMax, minOverlap);
  }

  else {
    fprintf(stderr, "-- Computing unitig consensus with errorRate %0.4f (max %0.4f) and minimum overlap "F_U32"\n",
            b, e, errorRate, errorRateMax, minOverlap);
  }

  fprintf(stderr, "\n");

  //  Create a consensus object.

  abAbacus  *abacus   = new abAbacus(gkpStore);


  //  I don't like this loop control.

  for (uint32 ti=b; (e == UINT32_MAX) || (ti <= e); ti++) {
    tgTig  *tig = NULL;

    if (tigStore)
      tig = tigStore->loadTig(ti);  //  Store owns the tig

    if (tigFile) {
      tig = new tgTig();  //  We own the tig

      if (tig->loadFromStreamOrLayout(tigFile) == false) {
        delete tig;
        break;
      }
    }

    if (tig == NULL)
      continue;

    //  Are we parittioned?  Is this tig in our partition?

    if (tigPart != UINT32_MAX) {
      uint32  missingReads = 0;

      for (uint32 ii=0; ii<tig->numberOfChildren(); ii++)
        if (gkpStore->gkStore_getReadInPartition(tig->getChild(ii)->ident()) == NULL)
          missingReads++;

      if (missingReads)
        continue;
    }

    if (tig->layoutLength() > maxLen) {
      fprintf(stderr, "SKIP unitig %d of length %d (%d children) - too long, skipped\n",
              tig->tigID(), tig->layoutLength(), tig->numberOfChildren());
      continue;
    }

    if (tig->numberOfChildren() == 0) {
      fprintf(stderr, "SKIP unitig %d of length %d (%d children) - no children, skipped\n",
              tig->tigID(), tig->layoutLength(), tig->numberOfChildren());
      continue;
    }

    bool exists = (tig->gappedLength() > 0);

    if (tig->numberOfChildren() > 1)
      fprintf(stderr, "Working on unitig %d of length %d (%d children)%s%s\n",
              tig->tigID(), tig->layoutLength(), tig->numberOfChildren(),
              ((exists == true)  && (forceCompute == false)) ? " - already computed"              : "",
              ((exists == true)  && (forceCompute == true))  ? " - already computed, recomputing" : "");

    //  Process the tig.  Remove deep coverage, create a consensus object, process it, and report the results.
    //  before we add it to the store.

    savedChildren    *origChildren = NULL;
    unitigConsensus  *utgcns       = NULL;
    bool              success      = exists;

    //  Compute consensus if it doesn't exist, or if we're forcing a recompute.

    if ((exists == false) || (forceCompute == true)) {
      tig->_utgcns_verboseLevel = verbosity;

      origChildren = stashContains(tig, maxCov);
      utgcns       = new unitigConsensus(gkpStore, errorRate, errorRateMax, minOverlap);

      success = utgcns->generate(tig, NULL);
    }

    //  If it was successful (or existed already), output.

    if (success) {
      if (showResult)
        tig->display(stdout, gkpStore, 200, 3);

      unstashContains(tig, origChildren);

      if (outResultsFile)
        tig->saveToStream(outResultsFile);

      if (outLayoutsFile)
        tig->dumpLayout(outLayoutsFile);

      if (outSeqFile)
        tig->dumpFASTQ(outSeqFile);
    }

    //  Report failures.

    if (success == false) {
      fprintf(stderr, "unitigConsensus()-- unitig %d failed.\n", tig->tigID());
      numFailures++;
    }

    //  Clean up, unloading or deleting the tig.

    delete utgcns;        //  No real reason to keep this until here.
    delete origChildren;  //  Need to keep it until after we display() above.

    if (tigStore)
      tigStore->unloadTig(tig->tigID(), true);  //  Tell the store we're done with it

    if (tigFile)
      delete tig;
  }

 finish:
  delete abacus;
  delete tigStore;
  delete gkpStore;

  if (tigFile)         fclose(tigFile);
  if (outResultsFile)  fclose(outResultsFile);
  if (outLayoutsFile)  fclose(outLayoutsFile);

  if (numFailures) {
    fprintf(stderr, "WARNING:  Total number of unitig failures = %d\n", numFailures);
    fprintf(stderr, "\n");
    fprintf(stderr, "Consensus did NOT finish successfully.\n");

  } else {
    fprintf(stderr, "Consensus finished successfully.  Bye.\n");
  }

  return(numFailures != 0);
}
