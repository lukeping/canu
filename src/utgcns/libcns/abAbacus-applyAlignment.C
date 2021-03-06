
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
 *    src/AS_CNS/ApplyAlignment.C
 *    src/AS_CNS/ApplyAlignment.c
 *    src/AS_CNS/MultiAlignment_CNS.c
 *    src/utgcns/libcns/ApplyAlignment.C
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
 *    Brian P. Walenz from 2005-JUN-16 to 2013-AUG-01
 *      are Copyright 2005-2011,2013 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Aaron Halpern from 2005-SEP-29 to 2006-OCT-03
 *      are Copyright 2005-2006 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Sergey Koren from 2008-FEB-27 to 2009-MAY-14
 *      are Copyright 2008-2009 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Brian P. Walenz beginning on 2014-NOV-17
 *      are Copyright 2014-2015 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

static char *rcsid = "$Id$";

#include "abAbacus.H"

#undef  DEBUG_FIND_BEAD
#undef  DEBUG_ALIGN_GAPS
#undef  DEBUG_ALIGN_POSITION
#undef  DEBUG_ABACUS_ALIGN


//  Add a column before cid, seeded with bead bid.
//
abColID
abAbacus::prependColumn(abColID cid, abBeadID bid) {
  abColID   col      = addColumn(getColumn(cid)->ma_id, bid);  //  Can reallocate column list.

  abColumn *column   = getColumn(col);
  abColumn *next     = getColumn(cid);

  abBead   *call     = getBead(column->callID());
  abBead   *nextcall = getBead(next->callID());

  //fprintf(stderr, "prependColumn()-- adding column for bid %d\n", bid);

  column->prev   = next->prev;
  column->next   = cid;

  call->prev     = nextcall->prev;
  call->next     = nextcall->ident();
  next->prev     = column->ident();

  nextcall->prev = call->ident();

  if (column->prevID().isValid())
    getColumn(column->prev)->next = column->ident();

  if (call->prev.isValid())
    getBead(call->prev)->next = call->ident();

  abColBeadIterator *ci = createColBeadIterator(cid);

  for (abBeadID  nid=ci->next(); nid.isValid(); nid=ci->next()) {
    abBead *bead = getBead(nid);

    //  The original version would not insert a gap bead at the start of a fragment.

    if ((bead->prev.isValid()) &&
        (bead->prev != bid))
      alignBeadToColumn(column->ident(), prependGapBead(nid), "prependColumn()");
  }

  column->ma_position = next->ma_position - 1;

  //AddColumnToMANode(column->ma_id, *column);
  getMultiAlign(column->ma_id)->addColumnToMultiAlign(column);

  //  Redundant
  //if (column->prevID().isValid() == false)
  //  getMultiAlign(column->ma_id)->first = column->lid;

  return(column->ident());
}





//  Returns the ident() of the bead that is:
//    in the same column as bead bi
//    in the same fragment as bead fi
//
abBeadID
findBeadInColumn(abAbacus *abacus, abBeadID bi, abBeadID fi) {

#ifdef DEBUG_FIND_BEAD
  fprintf(stderr, "findBeadInColumn bead bi="F_U32" bead fi="F_U32"\n", bi.get(), fi.get());
#endif

  if ((fi.isInvalid()) || (bi.isInvalid()))
    return(abBeadID());

  abSeqID ff = abacus->getBead(fi)->seqIdx();

#ifdef DEBUG_FIND_BEAD
  fprintf(stderr, "findBeadInColumn fragindex ff="F_S32"\n", ff.get());
#endif

  abBead *b = abacus->getBead(bi);

  if (b->seqIdx() == ff)
    return(bi);

  //  Search up.  The way we call findBeadInColumn, the one we're
  //  looking for is usually up from where we start.
  while (b->upID().isValid()) {
    b = abacus->getBead(b->upID());
#ifdef DEBUG_FIND_BEAD
    fprintf(stderr, "findBeadInColumn up bead="F_U32" ff=%d\n", b->ident().get(), b->seqIdx().get());
#endif
    if (b->seqIdx() == ff)
      return(b->ident());
  }

  //  Search down.
  b = abacus->getBead(bi);

  while (b->downID().isValid()) {
    b = abacus->getBead(b->downID());
#ifdef DEBUG_FIND_BEAD
    fprintf(stderr, "findBeadInColumn down bead="F_U64" ff=%d\n", b->ident().get(), b->seqIdx().get());
#endif
    if (b->seqIdx() == ff)
      return(b->ident());
  }

  //  Give up.  See comments in MultiAlignUnitig, where we append new sequence to the start of
  //  frankenstein ("Append the new stuff...").

  assert(0);
  return(abBeadID());
}



//  Add gaps already in abacus to our read -- complicated by possibly having to switch the fragment
//  the abead is working on.
//
//  Assumes we just aligned the 'a' to 'b' at the start.  Abacus already has a bunch of gap
//  beads/columns in it (these are NOT gaps in the alignment, or gaps in the sequence; they're
//  caused by other alignments) and we must add these columns to the B read.
//
//  A   a-----a
//  B   b
//
static
void
alignGaps(abAbacus *abacus,
          abBeadID *aindex, int32 &apos, int32  alen,
          abBeadID *bindex, int32 &bpos, int32  blen,
          abBeadID &lasta,
          abBeadID &lastb) {

  //  findBeadInColumn() will move us to the next 'a' fragment when we are at the end of the current
  //  one.
  //
  //  next    ->       .....x--********
  //  current ->  ***********--
  //
  //  * -- beads that we align to.
  //  x -- bead 'nn' below.
  //  - -- gap in abacus.
  //
  //  We're at the last * on the second row, the next bead we align to is the first * on the first
  //  row.  There might be gaps already in abacus that we need to add.  We move to the 'x', then
  //  walk along this 'a' fragment adding gaps.

  //#ifdef DEBUG_ALIGN_GAPS
  //  fprintf(stderr, "alignGaps()-- apos=%d alen=%d  bpos=%d blen=%d  lasta=%d  lastb=%d\n",
  //          apos, alen, bpos, blen, lasta.get(), lastb.get());
  //#endif

  if (apos >= alen)
    return;

  lasta = findBeadInColumn(abacus, lasta, aindex[apos]);

  if (lasta.isInvalid())
    return;

  abBeadID nexta = abacus->getBead(lasta)->nextID();

  //  If the next bead (from 'x' in the picture) is NOT the next base in A, add gaps until that is
  //  so.

  while (nexta != aindex[apos]) {
#ifdef DEBUG_ALIGN_GAPS
    fprintf(stderr, "alignGaps()-- lasta=%d  nexta=%d  search for aindex[apos]=%d  \n",
            lasta.get(), nexta.get(), aindex[apos].get());
#endif

    lastb = abacus->appendGapBead(lastb);
    lasta = nexta;

    abBead *bead = abacus->getBead(nexta);  //  AppendGapBead might reallocate beadStore

    abacus->alignBeadToColumn(bead->colIdx(), lastb, "applyAlignment(alignGaps)");

    nexta = bead->nextID();
  }
}


//  Aligns a bead in B to the existing column in A.
//
//  Called by applyAlignment().
//
static
void
alignPosition(abAbacus *abacus,
              abBeadID *aindex, int32 &apos, int32  alen,
              abBeadID *bindex, int32 &bpos, int32  blen,
              abBeadID &lasta,
              abBeadID &lastb,
              char   *label) {

  assert(apos < alen);
  assert(bpos < blen);

  abBead *bead = abacus->getBead(aindex[apos]);

#ifdef DEBUG_ALIGN_POSITION
  fprintf(stderr, "alignPosition()-- add %c to column %d apos=%d bpos=%d lasta=%d lastb=%d\n",
          abacus->getBase(bead->baseIdx()), bead->colIdx().get(), apos, bpos, lasta.get(), lastb.get());
#endif

  abacus->alignBeadToColumn(bead->colIdx(), bindex[bpos], label);

  lasta = aindex[apos];
  lastb = bindex[bpos];

  apos++;
  bpos++;

  alignGaps(abacus, aindex, apos, alen, bindex, bpos, blen, lasta, lastb);

  //assert(abacus->getBead(aindex[apos])->prev == bead->bindex);
  //assert(abacus->getBead(bindex[bpos])->prev == lastb);
}




void
abAbacus::applyAlignment(abSeqID   afid,
                         int32     alen, abBeadID *aindex,
                         abSeqID   bfid,
                         int32     ahang,
                         int32     bhang,
                         int32    *trace) {

  int32     bpos   = 0;  //MAX(bhang, 0);
  int32     blen   = 0;

  int32     apos   = MAX(ahang,0);

  abBeadID *bindex = NULL;

  abBeadID  lasta;
  abBeadID  lastb;

#ifdef DEBUG_ABACUS_ALIGN
  fprintf(stderr, "abAbacus::applyAlignment()-- ahang=%d bhang=%d trace=%d %d %d %d\n",
          ahang, bhang, trace[0], trace[1], trace[2], trace[3], trace[4]);
#endif

  if (afid.isValid()) {
    abSequence *afrag = getSequence(afid);
    alen              = afrag->length();
    aindex            = new abBeadID [alen];

    //  The loop really abuses the fact that all the beads for the bases in this read are
    //  contiguous.  They're contiguous because CreateMANode() (I think it's in there) allocated
    //  them in one block.
    //
    for (uint32 ai=0; ai<alen; ai++)
      aindex[ai].set(afrag->firstBead().get() + ai);

  } else {
    assert(aindex != NULL);
  }


  if (bfid.isValid()) {
    abSequence *bfrag = getSequence(bfid);
    blen              = bfrag->length();
    bindex            = new abBeadID [blen];

    for (uint32 bi=0; bi<blen; bi++)
      bindex[bi].set(bfrag->firstBead().get() + bi);

    //bfrag->multiAlignID() = abMultiAlignID();  //  USED?
  } else {
    assert(0);
  }

  assert(apos < alen);
  assert(bpos < blen);

  //
  //  All the a beads should be in a column.  All the b beads should not.
  //
  //sanityCheck((afid >= 0) ? getFragment(afid)->firstbead : abBeadID(),
  //            (bfid >= 0) ? getFragment(bfid)->firstbead : abBeadID());

  {
    uint32    columnErrors = 0;

    if (afid.isValid()) {
      abBead   *b = getBead(getSequence(afid)->firstBead());

      while (b) {
        if (b->column_index.isValid() == false) {
          columnErrors++;
          fprintf(stderr, "bead "F_U32" in A has undef column_index.\n",
                  b->ident().get());
        }
        b = (b->nextID().isInvalid()) ? NULL : getBead(b->nextID());
      }
    }

    if (bfid.isValid()) {
      abBead *b = getBead(getSequence(bfid)->firstBead());

      while (b) {
        if (b->column_index.isValid() == true) {
          columnErrors++;
          fprintf(stderr, "bead "F_U32" in B has defined column_index %d.\n",
                  b->ident().get(), b->column_index.get());
        }
        b = (b->nextID().isInvalid()) ? NULL : getBead(b->nextID());
      }
    }

    assert(columnErrors == 0);
  }



  //  Negative ahang?  push these things onto the start of abacus.  Fail if we get a negative ahang,
  //  but there is a column already before the first one (which implies we aligned to something not
  //  full-length, possibly because we trimmed frankenstein wrong).
  //
  if (ahang < 0) {
    abBead  *bead = getBead(aindex[0]);
    abColID  colp = bead->colIdx();

    //abacus->getBead(aindex[apos])->column_index

    assert(bead->prev.isInvalid());

    while (bpos < -ahang) {
      bead = getBead(bindex[bpos]);

#ifdef DEBUG_ABACUS_ALIGN
      fprintf(stderr, "ApplyAlignment()-- Prepend column for ahang bead=%d,%c\n",
              bead->ident().get(),
              getBase(bead->baseIdx()));
#endif
      prependColumn(colp, bindex[bpos++]);
    }

    lasta = getBead(aindex[0])->prev;
    lastb = bindex[bpos - 1];
  }


  //  trace is NULL for the first fragment, all we want to do there is load the initial abacus.
  //
  if (trace == NULL)
    goto loadInitial;


  //  Skip any positive traces at the start.  These are template sequence aligned to gaps in the
  //  read, but we don't care because there isn't any read aligned yet.

  while ((trace != NULL) && (*trace != 0) && (*trace == 1)) {
#ifdef DEBUG_ABACUS_ALIGN
    fprintf(stderr, "trace=%d  apos=%d alen=%d bpos=%d blen=%d - SKIP INITIAL GAP IN READ\n", *trace, apos, alen, bpos, blen);
#endif
    apos++;
    trace++;
  }


  while ((trace != NULL) && (*trace != 0)) {

#ifdef DEBUG_ABACUS_ALIGN
    fprintf(stderr, "trace=%d  apos=%d alen=%d bpos=%d blen=%d\n", *trace, apos, alen, bpos, blen);
#endif

    //
    //  Gap is in afrag.  align ( - *trace - apos ) positions
    //
    if ( *trace < 0 ) {
      while ( apos < (- *trace - 1))
        alignPosition(this, aindex, apos, alen, bindex, bpos, blen, lasta, lastb, "ApplyAlignment(1)");

      //  Insert a gap column to accommodate the insert in B.

      assert(apos < alen);
      assert(bpos < blen);

      //  Handle an initial -1 trace.  This bypasses the alignPosition above, which results in an
      //  invalid lasta.

      if ((lasta.isInvalid()) || (bpos == 0)) {
        assert(lasta.isInvalid());
        assert(bpos  == 0);

        prependColumn(getBead(aindex[apos])->column_index, bindex[bpos]);

        lasta = getBead(aindex[apos])->prev;
        lastb = bindex[bpos];
      } else {
        assert(lasta == getBead(aindex[apos])->prev);
        appendColumn(getBead(lasta)->colIdx(), bindex[bpos]);
        lasta = getBead(lasta)->nextID();
        lastb = bindex[bpos];
      }

      assert(lasta == getBead(aindex[apos])->prev);

      bpos++;
    }

    //
    //  Gap is in bfrag.  align ( *trace - bpos ) positions
    //
    if (*trace > 0) {
      while ( bpos < (*trace - 1) )
        alignPosition(this, aindex, apos, alen, bindex, bpos, blen, lasta, lastb, "ApplyAlignment(4)");

      //  Insert a gap bead into B, and align to the existing column.  This event is like
      //  alignPositions(), and we must continue aligning to existing gap columns in A.

      assert(apos < alen);
      assert(bpos < blen);

      //  Unlike the *trace < 0 case, we have already removed the initial +1 trace elements, so
      //  alignPosition() is always called, and lasta is always valid.

      assert(lasta.isInvalid() == false);

      lasta = getBead(lasta)->nextID();
      lastb = appendGapBead(lastb);

      assert(lasta == aindex[apos]);

      alignBeadToColumn(getBead(lasta)->colIdx(),
                        lastb,
                        "ApplyAlignment(6)");

      apos++;

      //  Continue aligning to existing gap columns in A.  Duplication from alignPosition.

      alignGaps(this, aindex, apos, alen, bindex, bpos, blen, lasta, lastb);
    }

    trace++;
  }

  //
  //  Remaining alignment contains no indels
  //
 loadInitial:
#ifdef DEBUG_ABACUS_ALIGN
  fprintf(stderr, "Align the remaining:  bpos=%d blen=%d apos=%d alen=%d\n",
          bpos, blen, apos, alen);
#endif

  for (int32 rem = MIN(blen - bpos, alen - apos); rem > 0; rem--)
    alignPosition(this, aindex, apos, alen, bindex, bpos, blen, lasta, lastb, "ApplyAlignment(8)");

  //
  //  Now just tack on the new sequence
  //

#ifdef DEBUG_ABACUS_ALIGN
  fprintf(stderr, "Append new sequence:  bpos=%d blen=%d apos=%d alen=%d\n",
          bpos, blen, apos, alen);
#endif

  if (blen - bpos > 0) {
    abColID ci = getBead(lastb)->colIdx();

    //  There shouldn't be any gaps left to insert...but there might be if we stop early above.  Old
    //  versions of this would simply stuff gaps into the B sequence for the rest of the existing
    //  multialign.  We'd prefer to fail.
    //
    for (abColumn *col = getColumn(ci); col->nextID().isValid(); col=getColumn(col->nextID()))
      fprintf(stderr, "ERROR!  Column ci="F_U32" has a next pointer ("F_U32")\n",
              col->ident().get(), col->nextID().get());

    assert(getColumn(ci)->nextID().isValid() == false);

    //  Add on trailing (dovetail) beads from b
    //
    for (int32 rem=blen-bpos; rem > 0; rem--) {

#ifdef DEBUG_ALIGN_POSITION
      abBead *bead = getBead(bindex[bpos]);
      fprintf(stderr, "alignPosition()-- add %c (bead %d seqIdx %d pos %d baseIdx %d) to column %d rem=%d bpos=%d blen=%d\n",
              getBase(bead->baseIdx()),
              bead->ident().get(),
              bead->seqIdx().get(),
              bead->foffset,
              bead->baseIdx().get(),
              bead->colIdx().get(),
              rem, bpos, blen);
#endif

      ci = appendColumn(ci, bindex[bpos++]);
    }
  }

  //CheckColumns();
#if 0
  {
    int32 fails = 0;
    int32 cmax  = GetNumColumns(columnStore);

    for (int32 cid=0; cid<cmax; cid++) {
      abColumn *column = getColumn(cid);

      if (column == NULL)
        continue;

      ColumnBeadIterator ci;
      CreateColumnBeadIterator(cid,&ci);

      Bead   *call = GetBead(beadStore,column->call);
      abBeadID  bid;

      while ( (bid = NextColumnBead(&ci)).isValid() ) {
        Bead *bead = GetBead(beadStore,bid);

        if (bead->colIdx() != cid)
          fails++;
      }
    }
    assert(fails == 0);
  }
#endif

  if (afid.isValid())  delete [] aindex;
  if (bfid.isValid())  delete [] bindex;

  //bfrag->manode = afrag->manode;
}
