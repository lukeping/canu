
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
 *  Modifications by:
 *
 *    Brian P. Walenz beginning on 2015-APR-15
 *      are Copyright 2015 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

const char *mainid = "$Id$";

#include "trimReads.H"
#include "clearRangeFile.H"

#include "AS_UTL_decodeRange.H"


//  Enforce any maximum clear range, if it exists (mbgn < mend)
//
//  There are six cases:
//
//       ---MAX-RANGE---
//   ---
//     -------------------
//     -----
//            -----
//                   -----
//                       ---
//
//  If the begin is below the max-bgn, we reset it to max-bgn.
//  If the end   is after the max-end, we reset it to max-end.
//
//  If after the resets we have an invalid clear (bgn > end)
//  the original clear range was completely outside the max range.
//
bool
enforceMaximumClearRange(ovOverlap      *ovl,
                         uint32           ovlLen,
                         gkRead          *read,
                         uint32           ibgn,
                         uint32           iend,
                         uint32          &fbgn,
                         uint32          &fend,
                         char            *logMsg,
                         clearRangeFile  *maxClr) {

  if (maxClr == NULL)
    return(true);

  if (fbgn == fend)
    return(true);

  uint32 mbgn = maxClr->bgn(read->gkRead_readID());
  uint32 mend = maxClr->end(read->gkRead_readID());

  assert(mbgn <  mend);
  assert(fbgn <= fend);

  if ((fend < mbgn) ||
      (mend < fbgn)) {
    //  Final clear not intersecting maximum clear.
    strcat(logMsg, (logMsg[0]) ? " - " : "\t");
    strcat(logMsg, "outside maximum allowed clear range");
    return(false);

  } else if ((fbgn < mbgn) ||
             (mend < fend)) {
    //  Final clear extends outside the maximum clear.
    fbgn = MAX(fbgn, mbgn);
    fend = MIN(fend, mend);

    strcat(logMsg, (logMsg[0]) ? " - " : "\t");
    strcat(logMsg, "adjusted to obey maximum allowed clear range");
    return(true);

  } else {
    //  Final clear already within the maximum clear.
    return(true);
  }
}



int
main(int argc, char **argv) {
  char             *gkpName = 0L;
  char             *ovsName = 0L;

  char             *iniClrName = NULL;
  char             *maxClrName = NULL;
  char             *outClrName = NULL;

  uint32            errorValue     = AS_OVS_encodeEvalue(0.015);
  uint32            minAlignLength = 40;
  uint32            minReadLength  = 64;

  char             *outputPrefix  = NULL;
  char              logName[FILENAME_MAX] = {0};
  char              sumName[FILENAME_MAX] = {0};
  FILE             *logFile = 0L;
  FILE             *sumFile = 0L;

  uint32            idMin = 1;
  uint32            idMax = UINT32_MAX;

  uint32            minEvidenceOverlap  = 40;
  uint32            minEvidenceCoverage = 1;

  argc = AS_configure(argc, argv);

  int arg=1;
  int err=0;
  while (arg < argc) {
    if        (strcmp(argv[arg], "-G") == 0) {
      gkpName = argv[++arg];

    } else if (strcmp(argv[arg], "-O") == 0) {
      ovsName = argv[++arg];

    } else if (strcmp(argv[arg], "-Ci") == 0) {
      iniClrName = argv[++arg];
    } else if (strcmp(argv[arg], "-Cm") == 0) {
      maxClrName = argv[++arg];
    } else if (strcmp(argv[arg], "-Co") == 0) {
      outClrName = argv[++arg];

    } else if (strcmp(argv[arg], "-e") == 0) {
      double erate = atof(argv[++arg]);
      errorValue = AS_OVS_encodeEvalue(erate);

    } else if (strcmp(argv[arg], "-l") == 0) {
      minAlignLength = atoi(argv[++arg]);

    } else if (strcmp(argv[arg], "-minlength") == 0) {
      minReadLength = atoi(argv[++arg]);

    } else if (strcmp(argv[arg], "-ol") == 0) {
      minEvidenceOverlap = atoi(argv[++arg]);

    } else if (strcmp(argv[arg], "-oc") == 0) {
      minEvidenceCoverage = atoi(argv[++arg]);

    } else if (strcmp(argv[arg], "-o") == 0) {
      outputPrefix = argv[++arg];

    } else if (strcmp(argv[arg], "-t") == 0) {
      AS_UTL_decodeRange(argv[++arg], idMin, idMax);

    } else {
      fprintf(stderr, "ERROR: unknown option '%s'\n", argv[arg]);
      err++;
    }

    arg++;
  }
  if ((gkpName       == NULL) ||
      (ovsName       == NULL) ||
      (outputPrefix  == NULL) ||
      (err)) {
    fprintf(stderr, "usage: %s -G gkpStore -O ovlStore -Co output.clearFile -o outputPrefix\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "  -G gkpStore    path to read store\n");
    fprintf(stderr, "  -O ovlStore    path to overlap store\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -o name        output prefix, for logging\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -t bgn-end     limit processing to only reads from bgn to end (inclusive)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -Ci clearFile  path to input clear ranges (NOT SUPPORTED)\n");
    //fprintf(stderr, "  -Cm clearFile  path to maximal clear ranges\n");
    fprintf(stderr, "  -Co clearFile  path to ouput clear ranges\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -e erate       ignore overlaps with more than 'erate' percent error\n");
    //fprintf(stderr, "  -l length      ignore overlaps shorter than 'l' aligned bases (NOT SUPPORTED)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -ol l          the minimum evidence overlap length\n");
    fprintf(stderr, "  -oc c          the minimum evidence overlap coverage\n");
    fprintf(stderr, "                   evidence overlaps must overlap by 'l' bases to be joined, and\n");
    fprintf(stderr, "                   must be at least 'c' deep to be retained\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -minlength l   reads trimmed below this many bases are deleted\n");
    fprintf(stderr, "\n");
    exit(1);
  }

  gkStore          *gkp = new gkStore(gkpName);
  ovStore          *ovs = new ovStore(ovsName, gkp);

  clearRangeFile   *iniClr = (iniClrName == NULL) ? NULL : new clearRangeFile(iniClrName, gkp);
  clearRangeFile   *maxClr = (maxClrName == NULL) ? NULL : new clearRangeFile(maxClrName, gkp);
  clearRangeFile   *outClr = (outClrName == NULL) ? NULL : new clearRangeFile(outClrName, gkp);

  if (outClr)
    //  If the outClr file exists, those clear ranges are loaded.  We need to reset them
    //  back to 'untrimmed' for now.
    outClr->reset(gkp);

  if (iniClr && outClr)
    //  An iniClr file was supplied, so use those as the initial clear ranges.
    outClr->copy(iniClr);


  if (outputPrefix) {
    sprintf(logName, "%s.log",     outputPrefix);
    sprintf(sumName, "%s.summary", outputPrefix);

    errno = 0;
    logFile = fopen(logName, "w");
    if (errno)
      fprintf(stderr, "Failed to open log file '%s' for writing: %s\n", logName, strerror(errno)), exit(1);

    sumFile = fopen(sumName, "w");
    if (errno)
      fprintf(stderr, "Failed to open summary file '%s' for writing: %s\n", sumName, strerror(errno)), exit(1);

    fprintf(logFile, "id\tinitL\tinitR\tfinalL\tfinalR\tmessage (DEL=deleted NOC=no change MOD=modified)\n");

    fprintf(sumFile, "Overlap error rate     <= %.4f fraction error\n", AS_OVS_decodeEvalue(errorValue));
    fprintf(sumFile, "Overlap min overlap    >= %u base%s (for 'largest covered')\n", minEvidenceOverlap,  (minEvidenceOverlap  == 1) ? "" : "s");
    fprintf(sumFile, "Overlap min coverage   >= %u read%s (for 'largest covered')\n", minEvidenceCoverage, (minEvidenceCoverage == 1) ? "" : "s");
  }


  uint32      ovlLen       = 0;
  uint32      ovlMax       = 64 * 1024;
  ovOverlap  *ovl          = ovOverlap::allocateOverlaps(gkp, ovlMax);

  memset(ovl, 0, sizeof(ovOverlap) * ovlMax);

  char        logMsg[1024] = {0};

  if (idMin < 1)
    idMin = 1;
  if (idMax > gkp->gkStore_getNumReads())
    idMax = gkp->gkStore_getNumReads();

  fprintf(stderr, "Processing from ID "F_U32" to "F_U32" out of "F_U32" reads.\n",
          idMin,
          idMax,
          gkp->gkStore_getNumReads());

  for (uint32 id=idMin; id<=idMax; id++) {
    gkRead     *read = gkp->gkStore_getRead(id);
    gkLibrary  *libr = gkp->gkStore_getLibrary(read->gkRead_libraryID());

    logMsg[0] = 0;

    //  If the fragment is deleted, do nothing.  If the fragment was deleted AFTER overlaps were
    //  generated, then the overlaps will be out of sync -- we'll get overlaps for these fragments
    //  we skip.
    //
    if ((iniClr) && (iniClr->isDeleted(id) == true))
      continue;

    //  If it did not request trimming, do nothing.  Similar to the above, we'll get overlaps to
    //  fragments we skip.
    //
    if ((libr->gkLibrary_finalTrim() == FINALTRIM_LARGEST_COVERED) &&
        (libr->gkLibrary_finalTrim() == FINALTRIM_BEST_EDGE))
      continue;

    //  Decide on the initial trimming.  We copied any iniClr into outClr above, and if there wasn't
    //  an iniClr, then outClr is the full read.

    uint32      ibgn   = outClr->bgn(id);
    uint32      iend   = outClr->end(id);

    //  Set the, ahem, initial final trimming.

    bool        isGood = false;
    uint32      fbgn   = ibgn;
    uint32      fend   = iend;

    //  Load overlaps.

    uint32      nLoaded = ovs->readOverlaps(id, ovl, ovlLen, ovlMax);

    //  Trim!

    if (nLoaded == 0) {
      //  No overlaps, so mark it as junk.
      isGood = false;
    }

    else if (libr->gkLibrary_finalTrim() == FINALTRIM_LARGEST_COVERED) {
      //  Use the largest region covered by overlaps as the trim

      assert(ovlLen > 0);
      assert(id == ovl[0].a_iid);

      isGood = largestCovered(ovl, ovlLen,
                              read,
                              ibgn, iend, fbgn, fend,
                              logMsg,
                              errorValue,
                              minEvidenceOverlap,
                              minEvidenceCoverage,
                              minReadLength);
      assert(fbgn <= fend);

    }

    else if (libr->gkLibrary_finalTrim() == FINALTRIM_BEST_EDGE) {
      //  Use the largest region covered by overlaps as the trim

      assert(ovlLen > 0);
      assert(id == ovl[0].a_iid);

      isGood = bestEdge(ovl, ovlLen,
                        read,
                        ibgn, iend, fbgn, fend,
                        logMsg,
                        errorValue,
                        minEvidenceOverlap,
                        minEvidenceCoverage,
                        minReadLength);
      assert(fbgn <= fend);

    }

    else {
      //  Do nothing.  Really shouldn't get here.
      assert(0);
      continue;
    }

    //  Enforce the maximum clear range

    if ((isGood) && (maxClr)) {
      isGood = enforceMaximumClearRange(ovl, ovlLen,
                                        read,
                                        ibgn, iend, fbgn, fend,
                                        logMsg,
                                        maxClr);
      assert(fbgn <= fend);
    }

    //
    //  Trimmed.  Make sense of the result, write some logs, and update the output.
    //


    //  If bad trimming or too small, write the log and keep going.
    //
    if ((isGood == false) || (fend - fbgn < minReadLength)) {
      outClr->setbgn(id) = fbgn;
      outClr->setend(id) = fend;
      outClr->setDeleted(id);  //  Gah, just obliterates the clear range.

      fprintf(logFile, F_U32"\t"F_U32"\t"F_U32"\t"F_U32"\t"F_U32"\tDEL%s\n",
              id,
              ibgn, iend,
              fbgn, fend,
              (logMsg[0] == 0) ? "" : logMsg);
    }

    //  If we didn't change anything, also write a log.
    //
    else if ((ibgn == fbgn) &&
        (iend == fend)) {
      fprintf(logFile, F_U32"\t"F_U32"\t"F_U32"\t"F_U32"\t"F_U32"\tNOC%s\n",
              id,
              ibgn, iend,
              fbgn, fend,
              (logMsg[0] == 0) ? "" : logMsg);
      continue;
    }

    //  Otherwise, we actually did something.

    else {
      outClr->setbgn(id) = fbgn;
      outClr->setend(id) = fend;

      fprintf(logFile, F_U32"\t"F_U32"\t"F_U32"\t"F_U32"\t"F_U32"\tMOD%s\n",
              id,
              ibgn, iend,
              fbgn, fend,
              (logMsg[0] == 0) ? "" : logMsg);
    }
  }

  delete gkp;
  delete ovs;

  delete iniClr;
  delete maxClr;
  delete outClr;

  fclose(logFile);
  fclose(sumFile);

  exit(0);
}
