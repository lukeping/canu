
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
 *    src/AS_CNS/MultiAlignUnitig.C
 *    src/AS_CNS/MultiAlignUnitig.c
 *    src/AS_CNS/MultiAlignment_CNS.c
 *    src/utgcns/libcns/MultiAlignUnitig.C
 *
 *  Modifications by:
 *
 *    Michael Schatz on 2004-SEP-23
 *      are Copyright 2004 The Institute for Genomics Research, and
 *      are subject to the GNU General Public License version 2
 *
 *    Jason Miller on 2005-MAR-22
 *      are Copyright 2005 The Institute for Genomics Research, and
 *      are subject to the GNU General Public License version 2
 *
 *    Eli Venter from 2005-MAR-30 to 2008-FEB-13
 *      are Copyright 2005-2006,2008 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Gennady Denisov from 2005-MAY-09 to 2008-JUN-06
 *      are Copyright 2005-2008 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Brian P. Walenz from 2005-JUN-16 to 2013-OCT-04
 *      are Copyright 2005-2013 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Aaron Halpern from 2005-SEP-29 to 2006-OCT-03
 *      are Copyright 2005-2006 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Sergey Koren from 2008-FEB-27 to 2009-JUN-05
 *      are Copyright 2008-2009 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Sergey Koren beginning on 2011-OCT-27
 *      are Copyright 2011 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *    Brian P. Walenz beginning on 2014-NOV-17
 *      are Copyright 2014-2015 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

static char *rcsid = "$Id$";

#include "unitigConsensus.H"

#include "NDalign.H"

#include <set>

using namespace std;



//  Define this.  Use the faster aligner from overlapper.  If not defined,
//  a full O(n^2) DP is computed.
//
#undef  WITH_NDALIGN
#define WITH_NDALIGN



unitigConsensus::unitigConsensus(gkStore  *gkpStore_,
                                 double    errorRate_,
                                 double    errorRateMax_,
                                 uint32    minOverlap_) {

  gkpStore        = gkpStore_;

  tig             = NULL;
  numfrags        = 0;
  trace           = NULL;
  abacus          = NULL;
  multialign      = abMultiAlignID();
  utgpos          = NULL;
  cnspos          = NULL;
  tiid            = 0;
  piid            = -1;

  frankensteinLen = 0;
  frankensteinMax = 0;
  frankenstein    = NULL;
  frankensteinBof = NULL;

  minOverlap      = minOverlap_;
  errorRate       = errorRate_;
  errorRateMax    = errorRateMax_;

  oaPartial       = NULL;
  oaFull          = NULL;
}


unitigConsensus::~unitigConsensus() {
  delete [] trace;
  delete    abacus;

  delete [] utgpos;
  delete [] cnspos;
  delete [] frankenstein;
  delete [] frankensteinBof;

  delete    oaPartial;
  delete    oaFull;
}




void
unitigConsensus::reportStartingWork(void) {
  if (showProgress())
    fprintf(stderr, "unitigConsensus()-- processing read %u/%u id %d pos %d,%d anchor %d,%d,%d -- length %u\n",
            tiid+1, numfrags,
            utgpos[tiid].ident(),
            utgpos[tiid].min(),
            utgpos[tiid].max(),
            utgpos[tiid].anchor(),
            utgpos[tiid].aHang(),
            utgpos[tiid].bHang(),
            abacus->getMultiAlign(multialign)->length());

  if (showPlacementBefore())
    for (int32 x=0; x<=tiid; x++)
      fprintf(stderr, "unitigConsensus()-- mid %10d  utgpos %7d,%7d  cnspos %7d,%7d  anchor %10d,%6d,%6d\n",
              utgpos[x].ident(),
              utgpos[x].min(), utgpos[x].max(),
              cnspos[x].min(), cnspos[x].max(),
              utgpos[x].anchor(), utgpos[x].aHang(), utgpos[x].bHang());
}


void
unitigConsensus::reportFailure(uint32 *failed) {
  if (failed != NULL)
    failed[tiid] = true;

  fprintf(stderr, "unitigConsensus()-- failed to align fragment %d in unitig %d.\n",
          utgpos[tiid].ident(), tig->tigID());
}


void
unitigConsensus::reportSuccess(uint32 *failed) {
  if (failed != NULL)
    failed[tiid] = false;

  //fprintf(stderr, "unitigConsensus()-- fragment %d aligned in unitig %d.\n",
  //        utgpos[tiid].ident(), tig->tigID());
}





bool
unitigConsensus::generate(tgTig     *tig_,
                          uint32    *failed_) {
  //bool  failuresToFix = false;

  tig      = tig_;
  numfrags = tig->numberOfChildren();

  if (initialize(failed_) == FALSE) {
    fprintf(stderr, "generateMultiAlignment()--  Failed to initialize for tig %u with %u children\n", tig->tigID(), tig->numberOfChildren());
    goto returnFailure;
  }

  while (moreFragments()) {
    reportStartingWork();

    //  First attempt, all default parameters

    if (computePositionFromAnchor()    && alignFragment())  goto applyAlignment;
    if (computePositionFromLayout()    && alignFragment())  goto applyAlignment;
    if (computePositionFromAlignment() && alignFragment())  goto applyAlignment;

    //  Second attempt, default parameters after recomputing consensus sequence.

    if (showAlgorithm())
      fprintf(stderr, "generateMultiAlignment()-- recompute full consensus\n");

    rebuild(true, showMultiAlignments());

    if (computePositionFromAnchor()    && alignFragment())  goto applyAlignment;
    if (computePositionFromLayout()    && alignFragment())  goto applyAlignment;
    if (computePositionFromAlignment() && alignFragment())  goto applyAlignment;

    //  Third attempot, use whatever aligns.

    if (computePositionFromAlignment() && alignFragment(true))  goto applyAlignment;

    //  Nope, failed to align.

    reportFailure(failed_);
    //failuresToFix = true;
    continue;

  applyAlignment:
    setErrorRate(errorRate);
    setMinOverlap(minOverlap);

    reportSuccess(failed_);
    applyAlignment();
    rebuild(false, showMultiAlignments());

    //  As long as the last read aligns, we aren't a failure.  Not the greatest way to detect if a
    //  unitig became disconnected, but easy.
    //failuresToFix = false;
  }

  //if (failuresToFix)
  //  goto returnFailure;

  generateConsensus();
  exportToTig();

  //  NEED TO UPDATE THE tgTig

  return(true);

 returnFailure:
  fprintf(stderr, "generateMultiAlignment()-- unitig %d FAILED.\n", tig->tigID());

  //  tgTig should have no changes.

  return(false);
}




int
unitigConsensus::initialize(uint32 *failed) {

  int32 num_columns = 0;
  //int32 num_bases   = 0;

  if (numfrags == 0) {
    fprintf(stderr, "unitigConsensus::initialize()-- unitig has no children.\n");
    return(false);
  }

  utgpos = new tgPosition [numfrags];
  cnspos = new tgPosition [numfrags];

  memcpy(utgpos, tig->getChild(0), sizeof(tgPosition) * numfrags);
  memcpy(cnspos, tig->getChild(0), sizeof(tgPosition) * numfrags);

  traceLen   = 0;
  trace      = new int32 [2 * AS_MAX_READLEN];

  traceABgn  = 0;
  traceBBgn  = 0;

  memset(trace, 0, sizeof(int32) * 2 * AS_MAX_READLEN);

  //manode   = CreateMANode(tig->tigID());
  abacus     = new abAbacus(gkpStore);

  for (int32 i=0; i<numfrags; i++) {
    if (failed != NULL)
      failed[i]  = true;

    //  Clear the cnspos position.  We use this to show it's been placed by consensus.

    cnspos[i].set(0, 0);

    //  Guess a nice ... totally unused value
    //num_bases   += (int32)ceil( (utgpos[i].max() - utgpos[i].min()) * (1 + 2 * errorRate));

    num_columns  = (utgpos[i].min() > num_columns) ? utgpos[i].min() : num_columns;
    num_columns  = (utgpos[i].max() > num_columns) ? utgpos[i].max() : num_columns;

    //  Allocate and initialize the beads for each fragment.  Beads are not
    //  inserted in the abacus here.

    abSeqID fid = abacus->addRead(gkpStore,
                                  utgpos[i].ident(),
                                  utgpos[i]._askip, utgpos[i]._bskip,
                                  utgpos[i].isReverse());

    //  If this is violated, then the implicit map from utgpos[] and cnspos[] to the unitig child
    //  list is incorrect.

    assert(fid.get() == i);

    //if (VERBOSE_MULTIALIGN_OUTPUT)
    //  fprintf(stderr,"unitigConsensus()-- Added fragment mid %d pos %d,%d in unitig %d to store at local id %d.\n",
    //          utgpos[i].ident(), utgpos[i].min(), utgpos[i].max(), tig->tigID(), fid);
  }

  //  Now that the reads exist, we can initialize the multialign, and add the first read to it.

  multialign = abacus->addMultiAlign(abSeqID(0));

  //  Check for duplicate reads

  {
    set<uint32>  dupFrag;

    for (uint32 i=0; i<numfrags; i++) {
      if (utgpos[i].isRead() == false) {
        fprintf(stderr, "unitigConsensus()-- Unitig %d FAILED.  Child %d is not a read.\n",
                tig->tigID(), utgpos[i].ident());
        return(false);
      }

      if (dupFrag.find(utgpos[i].ident()) != dupFrag.end()) {
        fprintf(stderr, "unitigConsensus()-- Unitig %d FAILED.  Child %d is a duplicate.\n",
                tig->tigID(), utgpos[i].ident());
        return(false);
      }

      dupFrag.insert(utgpos[i].ident());
    }
  }

  if (failed)
    failed[0] = false;

  frankensteinLen = 0;
  frankensteinMax = MAX(1024 * 1024, 2 * num_columns);
  frankenstein    = new char     [frankensteinMax];
  frankensteinBof = new abBeadID [frankensteinMax];

  //  Save columns
  {
    abBeadID  bidx = abacus->getSequence(0)->firstBead();
    abBead   *bead = abacus->getBead(bidx);

    while (bead) {
      frankenstein   [frankensteinLen] = abacus->getBase(bead->baseIdx());
      frankensteinBof[frankensteinLen] = bead->ident();

      frankensteinLen++;

      bead = (bead->nextID().isInvalid()) ? NULL : abacus->getBead(bead->nextID());
    }

    frankenstein   [frankensteinLen] = 0;
    frankensteinBof[frankensteinLen] = abBeadID();

    cnspos[0].set(0, frankensteinLen);
  }

  return(true);
}



int
unitigConsensus::computePositionFromAnchor(void) {

  assert(piid == -1);

  uint32 anchor = utgpos[tiid].anchor();

  if (anchor == 0)
    //  No anchor?!  Damn.
    goto computePositionFromAnchorFail;

  for (piid = tiid-1; piid >= 0; piid--) {
    abSequence *aseq = abacus->getSequence(piid);

    if (anchor != aseq->gkpIdent())
      //  Not the anchor.
      continue;

    if ((cnspos[piid].min() == 0) &&
        (cnspos[piid].max() == 0))
      //  Is the anchor, but that isn't placed.
      goto computePositionFromAnchorFail;

    if ((utgpos[piid].max() < utgpos[tiid].min()) ||
        (utgpos[tiid].max() < utgpos[piid].min())) {
      //  Is the anchor, and anchor is placed, but the anchor doesn't agree with the placement.
      if (showPlacement())
        fprintf(stderr, "computePositionFromAnchor()-- anchor %d at utg %d,%d doesn't agree with my utg %d,%d.  FAIL\n",
                anchor,
                utgpos[piid].min(), utgpos[piid].max(),
                utgpos[tiid].min(), utgpos[tiid].max());
      goto computePositionFromAnchorFail;
    }

    //  Scale the hangs by the change in the anchor size between bogart and consensus.

    double   anchorScale = (double)(cnspos[piid].max() - cnspos[piid].min()) / (double)(utgpos[piid].max() - utgpos[piid].min());

    if (showPlacement())
      fprintf(stderr, "computePositionFromAnchor()--  frag %u in anchor %u -- hangs %d,%d -- scale %f -- final hangs %.0f,%.0f\n",
              utgpos[tiid].ident(),
              utgpos[piid].ident(),
              utgpos[tiid].aHang(),
              utgpos[tiid].bHang(),
              anchorScale,
              utgpos[tiid].aHang() * anchorScale,
              utgpos[tiid].bHang() * anchorScale);

    cnspos[tiid].set(cnspos[piid].min() + utgpos[tiid].aHang() * anchorScale,
                     cnspos[piid].max() + utgpos[tiid].bHang() * anchorScale);

    //  Hmmm, but if we shrank the read too much, add back in some of the length.  We want to end up
    //  with the read scaled by anchorScale, and centered on the hangs.

    int32   fragmentLength = utgpos[tiid].max() - utgpos[tiid].min();

    if ((cnspos[tiid].min() >= cnspos[tiid].max()) ||
        (cnspos[tiid].max() - cnspos[tiid].min() < 0.75 * fragmentLength)) {
      int32  center = (cnspos[tiid].min() + cnspos[tiid].max()) / 2;

      if (showPlacement()) {
        fprintf(stderr, "computePositionFromAnchor()--  frag %u in anchor %u -- too short.  reposition around center %d with adjusted length %.0f\n",
                utgpos[tiid].ident(),
                utgpos[piid].ident(),
                center, fragmentLength * anchorScale);
      }

      cnspos[tiid].set(center - fragmentLength * anchorScale / 2,
                       center + fragmentLength * anchorScale / 2);

      //  We seem immune to having a negative position.  We only use this to pull out a region from
      //  the partial consensus to align to.
      //
      //if (cnspos[tiid].min() < 0) {
      //  cnspos[tiid].min() = 0;
      //  cnspos[tiid].max() = fragmentLength * anchorScale;
      //}
    }

    assert(cnspos[tiid].min() < cnspos[tiid].max());

    if (showPlacement())
      fprintf(stderr, "computePositionFromAnchor()-- anchor %d at %d,%d --> beg,end %d,%d (fLen %d)\n",
              anchor,
              cnspos[piid].min(), cnspos[piid].max(),
              cnspos[tiid].min(), cnspos[tiid].max(),
              frankensteinLen);
    return(true);
  }

 computePositionFromAnchorFail:
  cnspos[tiid].set(0, 0);

  piid = -1;

  return(false);
}



int
unitigConsensus::computePositionFromLayout(void) {
  int32   thickestLen = 0;

  assert(piid == -1);

  //  Find the thickest qiid overlap to any cnspos fragment
  for (int32 qiid = tiid-1; qiid >= 0; qiid--) {
    if ((utgpos[tiid].min() < utgpos[qiid].max()) &&
        (utgpos[tiid].max() > utgpos[qiid].min()) &&
        ((cnspos[qiid].min() != 0) ||
         (cnspos[qiid].max() != 0))) {
      cnspos[tiid].set(cnspos[qiid].min() + utgpos[tiid].min() - utgpos[qiid].min(),
                       cnspos[qiid].max() + utgpos[tiid].max() - utgpos[qiid].max());

      //  This assert triggers.  It results in 'ooo' below being negative, and we
      //  discard this overlap anyway.
      //
      //assert(cnspos[tiid].min() < cnspos[tiid].max());

      int32 ooo = MIN(cnspos[tiid].max(), frankensteinLen) - cnspos[tiid].min();

#if 1
      if (showPlacement())
        fprintf(stderr, "computePositionFromLayout()-- layout %d at utg %d,%d cns %d,%d --> utg %d,%d cns %d,%d -- overlap %d\n",
                utgpos[qiid].ident(),
                utgpos[qiid].min(), utgpos[qiid].max(), cnspos[qiid].min(), cnspos[qiid].max(),
                utgpos[tiid].min(), utgpos[tiid].max(), cnspos[tiid].min(), cnspos[tiid].max(),
                ooo);
#endif

      //  Occasionally we see an overlap in the original placement (utgpos overlap) by after
      //  adjusting our fragment to the frankenstein position, we no longer have an overlap.  This
      //  seems to be caused by a bad original placement.
      //
      //  Example:
      //  utgpos[a] = 13480,14239    cnspos[a] = 13622,14279
      //  utgpos[b] = 14180,15062
      //
      //  Our placement is 200bp different at the start, but close at the end.  When we compute the
      //  new start placement, it starts after the end of the A read -- the utgpos say the B read
      //  starts 700bp after the A read, which is position 13622 + 700 = 14322....50bp after A ends.

      if ((cnspos[tiid].min() < frankensteinLen) &&
          (thickestLen < ooo)) {
        thickestLen = ooo;

        assert(cnspos[tiid].min() < cnspos[tiid].max());  //  But we'll still assert cnspos is ordered correctly.

        int32 ovl   = ooo;
        int32 ahang = cnspos[tiid].min();
        int32 bhang = cnspos[tiid].max() - frankensteinLen;

        piid  = qiid;
      }
    }
  }

  //  If we have a VALID thickest placement, use that (recompute the placement that is likely
  //  overwritten -- ahang, bhang and piid are still correct).

  if (thickestLen >= minOverlap) {
    assert(piid != -1);

    cnspos[tiid].set(cnspos[piid].min() + utgpos[tiid].min() - utgpos[piid].min(),
                     cnspos[piid].max() + utgpos[tiid].max() - utgpos[piid].max());

    assert(cnspos[tiid].min() < cnspos[tiid].max());

    if (showPlacement())
      fprintf(stderr, "computePositionFromLayout()-- layout %d at %d,%d --> beg,end %d,%d (fLen %d)\n",
              utgpos[piid].ident(),
              cnspos[piid].min(), cnspos[piid].max(),
              cnspos[tiid].min(), cnspos[tiid].max(),
              frankensteinLen);

    return(true);
  }

  cnspos[tiid].set(0, 0);

  piid = -1;

  return(false);
}



//  Occasionally we get a fragment that just refuses to go in the correct spot.  Search for the
//  correct placement in all of frankenstein, update ahang,bhang and retry.
//
//  We don't expect to have big negative ahangs, and so we don't allow them.  To unlimit this, use
//  "-fragmentLen" instead of the arbitrary cutoff below.
int
unitigConsensus::computePositionFromAlignment(void) {

  assert(piid == -1);

  int32        minlen      = minOverlap;
  int32        ahanglimit  = -10;

  abSequence  *seq         = abacus->getSequence(tiid);
  char        *fragment    = abacus->getBases(seq);
  int32        fragmentLen = seq->length();

  bool         foundAlign  = false;

  //
  //  Try NDalign.
  //

  if (foundAlign == false) {

    if (oaPartial == false)
      oaPartial = new NDalign(pedLocal, errorRate, 17);  //  partial allowed!

    oaPartial->initialize(0, frankenstein, frankensteinLen, 0, frankensteinLen,
                          1, fragment,     fragmentLen,     0, fragmentLen,
                          false);

    if ((oaPartial->findMinMaxDiagonal(minOverlap) == true) &&
        (oaPartial->findSeeds(false)               == true) &&
        (oaPartial->findHits()                     == true) &&
        (oaPartial->chainHits()                    == true) &&
        (oaPartial->processHits()                  == true)) {

      cnspos[tiid].set(oaPartial->abgn(), oaPartial->aend());
      fprintf(stderr, "cnspos[%3d] mid %d %d,%d (from NDalign)\n", tiid, utgpos[tiid].ident(), cnspos[tiid].min(), cnspos[tiid].max());

      foundAlign = true;
    }
  }

  //
  //  Fail.
  //

  if (foundAlign == false) {
    cnspos[tiid].set(0, 0);
    piid = -1;

    if (showAlgorithm())
      fprintf(stderr, "computePositionFromAlignment()-- Returns fail (no alignment).\n");
    return(false);
  }

  //  From the overlap and existing placements, find the thickest overlap, to set the piid and
  //  hangs, then reset the original placement based on that anchors original placement.
  //
  //  To work with fixFailures(), we need to scan the entire fragment list.  This isn't so bad,
  //  really, since before we were scanning (on average) half of it.

  assert(cnspos[tiid].min() < cnspos[tiid].max());

  int32   thickestLen = 0;

  for (int32 qiid = numfrags-1; qiid >= 0; qiid--) {
    if ((tiid != qiid) &&
        (cnspos[tiid].min() < cnspos[qiid].max()) &&
        (cnspos[tiid].max() > cnspos[qiid].min())) {
      int32 ooo = (MIN(cnspos[tiid].max(), cnspos[qiid].max()) -
                   MAX(cnspos[tiid].min(), cnspos[qiid].min()));

      if (thickestLen < ooo) {
        thickestLen = ooo;

        int32 ovl   = ooo;
        int32 ahang = cnspos[tiid].min();
        int32 bhang = cnspos[tiid].max() - frankensteinLen;

        piid  = qiid;
      }
    }
  }

  //  No thickest?  Dang.

  if (thickestLen == 0) {
    cnspos[tiid].set(0, 0);
    piid = -1;
    if (showAlgorithm())
      fprintf(stderr, "computePositionFromAlignment()-- Returns fail (no thickest).\n");
    return(false);
  }

  //  Success, yay!

  assert(piid != -1);

  if (showPlacement())
    fprintf(stderr, "computePositionFromAlignment()-- layout %d at %d,%d --> beg,end %d,%d (fLen %d)\n",
            utgpos[piid].ident(),
            cnspos[piid].min(), cnspos[piid].max(),
            cnspos[tiid].min(), cnspos[tiid].max(),
            frankensteinLen);

  return(true);
}


void
unitigConsensus::rebuild(bool recomputeFullConsensus, bool display) {
  abMultiAlign *ma = abacus->getMultiAlign(multialign);

  //  Run abacus to rebuild an intermediate consensus sequence.  VERY expensive.
  //
  if (recomputeFullConsensus == true) {
    abacus->refreshMultiAlign(multialign);

    ma->refine(abacus, abAbacus_Smooth);
    ma->mergeRefine(abacus, false);

    ma->refine(abacus, abAbacus_Poly_X);
    ma->mergeRefine(abacus, false);

    ma->refine(abacus, abAbacus_Indel);
    ma->mergeRefine(abacus, false);
  }

  //  For each column, vote for the consensus base to use.  Ideally, if we just computed the full
  //  consensus, we'd use that and just replace gaps with N.

  abColID cid   = ma->firstColumn();
  int32   index = 0;

  ma->columns().clear();

  frankensteinLen = 0;

  while (cid.isValid()) {
    abColumn *column = abacus->getColumn(cid);

    //  This is massively expensive!  It iterates over every bead in the column,
    //  summing the counts.  Really, it does what it says: it recomputes the base
    //  counts and compares against the saved version.
    //column->CheckBaseCounts(abacus);

    int32   nA = column->GetColumnBaseCount('A');
    int32   nC = column->GetColumnBaseCount('C');
    int32   nG = column->GetColumnBaseCount('G');
    int32   nT = column->GetColumnBaseCount('T');
    int32   nN = column->GetColumnBaseCount('N');
    int32   n_ = column->GetColumnBaseCount('-');
    int32   nn = 0;

    abBead *bead = abacus->getBead(column->callID());
    char    call = 'N';

    //fprintf(stderr, "Column %d %c ACGTN = %d %d %d %d %d\n", column->position(), abacus->getBase(bead->ident()), nA, nC, nG, nT, nN);

    if (nA > nn) { nn = nA;  call = 'A'; }
    if (nC > nn) { nn = nC;  call = 'C'; }
    if (nG > nn) { nn = nG;  call = 'G'; }
    if (nT > nn) { nn = nT;  call = 'T'; }

    //  Call should have been a gap, but we'll instead pick the most prevalant base, but lowercase
    //  it.  This is used by the dynamic programming alignment.
    if (n_ > nn)
      call = tolower(call);

    abacus->setBase(bead->baseIdx(), call);

    while (frankensteinLen >= frankensteinMax)
      resizeArrayPair(frankenstein, frankensteinBof, frankensteinLen, frankensteinMax, frankensteinMax * 2);

    assert(frankensteinLen < frankensteinMax);

    frankenstein   [frankensteinLen] = call;
    frankensteinBof[frankensteinLen] = bead->ident();
    frankensteinLen++;

    column->position() = index++;

    ma->columns().push_back(cid);

    cid = column->nextID();
  }

  frankenstein   [frankensteinLen] = 0;
  frankensteinBof[frankensteinLen] = abBeadID();

  //  Update the position of each fragment in the consensus sequence.

  for (int32 i=0; i<=tiid; i++) {
    if ((cnspos[i].min() == 0) &&
        (cnspos[i].max() == 0))
      //  Uh oh, not placed originally.
      continue;

    abSequence *seq   = abacus->getSequence(i);
    abBeadID    fbead = seq->firstBead();
    abBeadID    lbead = seq->lastBead();
    abColumn   *fcol  = abacus->getColumn(fbead);
    abColumn   *lcol  = abacus->getColumn(lbead);

    cnspos[i].set(fcol->position(),
                  lcol->position() + 1);

    assert(cnspos[i].min() >= 0);
    assert(cnspos[i].max() > cnspos[i].min());
  }

  //  Finally, update the anchor/hang of the fragment we just placed.

  if (piid >= 0)
    utgpos[tiid].set(utgpos[piid].ident(),
                     cnspos[tiid].min() - cnspos[piid].min(),
                     cnspos[tiid].max() - cnspos[piid].max());

  piid = -1;

  if (display)
    abacus->getMultiAlign(multialign)->display(abacus, stderr);
}


int
unitigConsensus::alignFragment(bool forceAlignment) {
  int32         bgnExtra     = 0;
  int32         endExtra     = 0;

  assert((cnspos[tiid].min() != 0) || (cnspos[tiid].max() != 0));
  assert(piid != -1);

  assert(cnspos[tiid].min() < cnspos[tiid].max());

  //  Compute how much extra consensus sequence to align to.  If we allow too much, we risk placing
  //  the fragment in an incorrect location -- worst case is that we find a longer higher scoring
  //  alignment, but lower identity that is rejected.  If we allow too little, we leave unaligned
  //  bits on the end of the fragment -- worst case, we have 1 extra base which might add an
  //  unnecessary gap.
  //
  //  The default used to be 100bp, which made sense for Sanger reads (maybe) but makes no sense for
  //  short reads of length say 100bp.
  //
  //  We are placing this read relative to some other read:
  //
  //      anchoring fragment   -----------------------------------------    ediff
  //      new fragment           bdiff   ------------------------------------------------
  //
  //  So we should allow errorRate indel in those relative positionings.
  //

  bgnExtra = (int32)ceil(errorRate * (cnspos[tiid].min() - cnspos[piid].min()));
  endExtra = (int32)ceil(errorRate * (cnspos[tiid].max() - cnspos[piid].max()));

  if (bgnExtra < 0)  bgnExtra = -bgnExtra;
  if (endExtra < 0)  endExtra = -endExtra;

  if (bgnExtra < 10) bgnExtra = 10;
  if (endExtra < 10) endExtra = 10;

  //  And compute how much extra fragment sequence to align to.  We want to trim off some
  //  of the bhang sequence to prevent false alignments.  We trim off all of the bhang sequence,
  //  except for 6% of the aligned length.
  //
  //  Actual example:  the real alignment should be this:
  //
  //  CGGCAGCCACCCCATCCGGGAGGGAGATGGGGGGGTCAGCCCCCCGCCCGGCCAGCCG
  //            CCCATCCGGGAGGGAGGTGGGGGGGTCAGCCCCCCGCCCCGCCAGCCGCTCCGTCCGGGAGGGAGGTGGGGGGGTCAGCCCCCCGCCCGGCCAGCCGCCCC
  //
  //  Instead, this higher scoring (longer) and noiser alignment was found.
  //
  //                                                   CGGCAGCCACCCCATCCGGGAGGGAGATGGGGGGGTCAGCCCCCCGCCCGGCCAGCCG
  //            CCCATCCGGGAGGGAGGTGGGGGGGTCAGCCCCCCGCCCCGCCAGCCGCTCCGTCCGGGAGGGAGGTGGGGGGGTCAGCCCCCCGCCCGGCCAGCCGCCCC
  //

  assert(cnspos[tiid].min() < cnspos[tiid].max());

  int32 cnsbgn = (cnspos[tiid].min() < cnspos[tiid].max()) ? cnspos[tiid].min() : cnspos[tiid].max();
  int32 cnsend = (cnspos[tiid].min() < cnspos[tiid].max()) ? cnspos[tiid].max() : cnspos[tiid].min();

  int32 endTrim = (cnsend - frankensteinLen) - (int32)ceil(errorRate * (cnsend - cnsbgn));

  if (endTrim < 20)  endTrim = 0;

  bool  tryAgain     = false;

 alignFragmentAgain:
  int32 frankBgn     = MAX(0, cnspos[tiid].min() - bgnExtra);   //  Start position in frankenstein
  int32 frankEnd     = frankensteinLen;                         //  Truncation of frankenstein
  char  frankEndBase = 0;                                       //  Saved base from frankenstein

  if (showAlgorithm())
    fprintf(stderr, "alignFragment()-- Allow bgnExtra=%d and endExtra=%d (frankBgn=%d frankEnd=%d) and endTrim=%d\n",
            bgnExtra, endExtra, frankBgn, frankEnd, endTrim);

  bool  allowAhang = false;
  bool  allowBhang = true;

  //  If the expected fragment begin position plus any extra slop is still the begin of the
  //  consensus sequence, we allow the fragment to hang over the end.
  //
  if (frankBgn == 0)
    allowAhang = true;

  //  If the expected fragment end position plus any extra slop are less than the consensus length,
  //  we need to truncate the frankenstein so we don't incorrectly align to that.
  //
  if (cnspos[tiid].max() + endExtra < frankEnd) {
    frankEnd     = cnspos[tiid].max() + endExtra;
    frankEndBase = frankenstein[frankEnd];
    frankenstein[frankEnd] = 0;
    allowBhang   = false;

  } else {
    endExtra = 0;
  }

  char       *aseq  = frankenstein + frankBgn;

  abSequence *bSEQ  = abacus->getSequence(tiid);
  char       *bseq  = abacus->getBases(bSEQ);

  if (endTrim >= bSEQ->length())
    fprintf(stderr, "alignFragment()-- ERROR -- excessive endTrim %d >= length %d\n", endTrim, bSEQ->length());
  if (endTrim < 0)
    fprintf(stderr, "alignFragment()-- ERROR -- negative endTrim %d\n", endTrim);

  //
  //  Try NDalign
  //

#ifdef WITH_NDALIGN
  if ((endTrim <  bSEQ->length()) &&
      (0       <= endTrim)) {
    int32 fragBgn      = 0;
    int32 fragEnd      = bSEQ->length() - endTrim;
    char  fragEndBase  = bseq[fragEnd];

    bseq[fragEnd] = 0;  //  Terminate the bseq early for alignment.

    //  Create new aligner object.  'Global' in this case just means to not stop early, not a true global alignment.

    if (oaFull == false)
      oaFull = new NDalign(pedGlobal, errorRate, 17);

    oaFull->initialize(0, aseq, frankEnd - frankBgn, 0, frankEnd - frankBgn,
                       1, bseq, fragEnd  - fragBgn,  0, fragEnd  - fragBgn,
                       false);

    //  Don't use seeds.
    bool  alignFound = oaFull->makeNullHit();

    //  While we find alignments, decide if they're any good, and stop on the first good one.  Try up
    //  to four seeds, then give up and fall back to dynamic programming.

    for (uint32 na=0; ((na                    <  1) &&
                       (alignFound            == true) &&
                       (oaFull->processHits() == true)); na++) {

      if (showAlignments())
        oaFull->display("unitigConsensus::alignFragment()--", true);

      //  Fix up non-dovetail alignments?  Nope, just skip them for now.  Eventually, we'll want to
      //  accept these (if long enough) by trimming the read.  To keep the unitig connected, we
      //  probably can only trim on the 5' end.

      if (oaFull->type() != pedDovetail) {
        if (showAlgorithm())
          fprintf(stderr, "unitigConsensus::alignFragment()-- alignment not dovetail, continue to next seed\n");
        continue;
      }

      //  Realign, from both endpoints, and save the better of the two.

      oaFull->realignBackward(showAlgorithm(), showAlignments());
      oaFull->realignForward (showAlgorithm(), showAlignments());

      if ((forceAlignment == false) &&
          (oaFull->scanDeltaForBadness(showAlgorithm(), showAlignments()) == true)) {
        if (showAlgorithm())
          fprintf(stderr, "unitigConsensus::alignFragment()-- alignment still bad, continue to next seed\n");
        continue;
      }

      //  Bad alignment if low quality.

      if ((forceAlignment == false) &&
          (oaFull->erate() > errorRate)) {
        if (showAlgorithm()) {
          fprintf(stderr, "unitigConsensus::alignFragment()-- alignment is low quality: %f > %f, continue to next seed\n",
                  oaFull->erate(), errorRate);
          oaFull->display("unitigConsensus::alignFragment()-- ", true);
        }
        continue;
      }

      //  Otherwise, its a good alignment.  Process the trace to the 'consensus-format' and return true.

      traceLen = 0;

      //  Set the begn points of the trace.  We probably need at least one of abgn and bbgn to be
      //  zero.  If both are nonzero, then we have a branch in the alignment.
      //
      //  If traceABgn is negative, we insert gaps into A before it starts, but I'm not sure how that works.

      assert((oaFull->abgn() == 0) ||
             (oaFull->bbgn() == 0));

      traceABgn = frankBgn + oaFull->abgn() - oaFull->bbgn();
      traceBBgn =            oaFull->bbgn();

      int32   apos = oaFull->abgn();
      int32   bpos = oaFull->bbgn();

      //  Overlap encoding:
      //    Add N matches or mismatches.  If negative, insert a base in the first sequence.  If
      //    positive, delete a base in the first sequence.
      //
      //  Consensus encoding:
      //    If negative, align (-trace - apos) bases, then add a gap in A.
      //    If positive, align ( trace - bpos) bases, then add a gap in B.
      //
      for (uint32 ii=0; ii<oaFull->deltaLen(); ii++, traceLen++) {
        if (oaFull->delta()[ii] < 0) {
          apos += -oaFull->delta()[ii] - 1;
          bpos += -oaFull->delta()[ii];

          trace[traceLen] = -apos - frankBgn - 1;

        } else {
          apos +=  oaFull->delta()[ii];
          bpos +=  oaFull->delta()[ii] - 1;  // critical

          trace[traceLen] = bpos + 1;
        }
      }

      trace[traceLen] = 0;

      if (frankEndBase)   frankenstein[frankEnd] = frankEndBase;  //  Restore bases we removed for alignment
      if (fragEndBase)    bseq[fragEnd]          = fragEndBase;

      return(true);
    }

    //  Well, shucks, we failed to find any decent alignment.

    if (frankEndBase)   frankenstein[frankEnd] = frankEndBase;  //  Restore bases we removed for alignment
    if (fragEndBase)    bseq[fragEnd]          = fragEndBase;
  }
#endif

  //
  //  Try Optimal_Overlap_AS_forCNS
  //

#if 0
  if ((endTrim <  bSEQ->length()) &&
      (0       <= endTrim)) {
    int32 fragBgn      = 0;
    int32 fragEnd      = bSEQ->length() - endTrim;
    char  fragEndBase  = bseq[fragEnd];

    bseq[fragEnd] = 0;

    ALNoverlap  *O           = NULL;
    int32        minlen      = minOverlap;

    if (showAlgorithm())
      fprintf(stderr, "unitigConsensus::alignFragment()-- Fall back to Optimal_Overlap_AS_forCNS\n");

    if (O == NULL) {
      O = Optimal_Overlap_AS_forCNS(aseq,
                                    bseq,
                                    0, frankEnd - frankBgn,   //  ahang bounds are unused here
                                    0, 0,                     //  ahang, bhang exclusion
                                    0,
                                    errorRate + 0.02, 1e-3, minlen,
                                    AS_FIND_ALIGN);
      if ((O) && (showAlignments()))
        PrintALNoverlap("Optimal_Overlap", aseq, bseq, O);
    }

    //  At 0.06 error, this equals the previous value of 10.
#if 0
    double  pad = errorRate * 500.0 / 3;


    //  If a negative ahang, extend the frankenstein and try again.

    if ((O) && (O->begpos < 0) && (frankBgn > 0)) {
      bgnExtra += -O->begpos + pad;
      tryAgain = true;
      O = NULL;
    }

    //  If ....what, extend the frankenstein and try again.

    if ((O) && (O->endpos > 0) && (allowBhang == false)) {
      endExtra += O->endpos + pad;
      tryAgain = true;
      O = NULL;
    }

    //  If postive bhang, trim the read and try again.

    if ((O) && (O->endpos < 0) && (endTrim > 0)) {
      endTrim -= -O->endpos + pad;
      if (endTrim < 20)
        endTrim = 0;
      tryAgain = true;
      O = NULL;
    }

    //  Too noisy?  Nope, don't want it.

    if (((double)O->diffs / (double)O->length) > errorRate) {
      //if (showAlgorithm())
      //  fprintf(stderr, "rejectAlignment()-- No alignment found -- erate %f > max allowed %f.\n",
      //          (double)O->diffs / (double)O->length, errorRate);
      O = NULL;
    }

    //  Too short?  Nope, don't want it.

    if (O->length < minOverlap) {
      //if (showAlgorithm())
      //  fprintf(stderr, "rejectAlignment()-- No alignment found -- too short %d < min allowed %d.\n",
      //          O->length, minOverlap);
      O = NULL;
    }
#endif

    //  Restore the bases we might have removed.
    if (frankEndBase)   frankenstein[frankEnd] = frankEndBase;
    if (fragEndBase)    bseq[fragEnd]          = fragEndBase;

    if (O) {
      traceLen = 0;

      traceABgn = frankBgn + O->begpos;
      traceBBgn = 0;

      for (int32 *t = O->trace; (t != NULL) && (*t != 0); t++) {
        if (*t < 0)
          *t -= frankBgn;
        trace[traceLen++] = *t;
      }

      trace[traceLen] = 0;

      if (showAlgorithm())
        fprintf(stderr, "alignFragment()-- Alignment succeeded.\n");

      return(true);
    }

    if (tryAgain)
      goto alignFragmentAgain;
  }  //  end of optimal
#endif

  //  No alignment.  Dang.  (Should already be 0,0, but just in case...)

  cnspos[tiid].set(0, 0);
  piid = -1;

  if (showAlgorithm())
    fprintf(stderr, "alignFragment()-- No alignment found.\n");

  return(false);
}




void
unitigConsensus::applyAlignment(void) {

  //fprintf(stderr, "traceLen %d traceABgn %d traceBBgn %d --", traceLen, traceABgn, traceBBgn);
  //for (uint32 ii=0; ii<32; ii++)
  //  fprintf(stderr, " %d", trace[ii]);
  //fprintf(stderr, "\n");

  abacus->applyAlignment(abSeqID(),
                         frankensteinLen, frankensteinBof,
                         tiid,
                         traceABgn, traceBBgn, trace);

  //  Doesn't show latest read added.
  //abacus->getMultiAlign(multialign)->display(abacus, stderr);
}


void
unitigConsensus::generateConsensus(void) {
  abMultiAlign *ma = abacus->getMultiAlign(multialign);

  //fprintf(stderr, "generateConsensus()-- length %u\n", ma->length());

  abacus->refreshMultiAlign(multialign, true, true);

  ma->refine(abacus, abAbacus_Smooth);
  ma->mergeRefine(abacus, true);

  ma->refine(abacus, abAbacus_Poly_X);
  ma->mergeRefine(abacus, true);

  ma->refine(abacus, abAbacus_Indel);
  ma->mergeRefine(abacus, true);

  //  Why is columnList getting screwed up?
  abacus->refreshMultiAlign(multialign, true, true);

  //ma->display(abacus, stderr, 0, 5000);

#if 0
  //  While we have fragments in memory, compute the microhet probability.  Ideally, this would be
  //  done in CGW when loading unitigs (the only place the probability is used) but the code wants
  //  to load sequence and quality for every fragment, and that's too expensive.
  {
    char   **multia = NULL;
    int32  **id_array = NULL;

    int32 depth = MANode2Array(manode, &multia, &id_array,0);

    ma->data.unitig_microhet_prob = 1.0;  //AS_REZ_MP_MicroHet_prob(multia, id_array, gkpStore, frankensteinLen, depth);

    for (int32 i=0;i<depth;i++) {
      delete [] multia[2*i];
      delete [] multia[2*i+1];
      delete [] id_array[i];
    }
    delete [] multia;
    delete [] id_array;
  }
#endif

}




void
unitigConsensus::exportToTig(void) {
  abMultiAlign *ma = abacus->getMultiAlign(multialign);

  ma->getConsensus(abacus, tig);
  ma->getPositions(abacus, tig);
}
