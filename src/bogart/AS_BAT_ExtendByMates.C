
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
 *    src/AS_BAT/AS_BAT_ExtendByMates.C
 *
 *  Modifications by:
 *
 *    Brian P. Walenz from 2012-JAN-05 to 2014-JAN-28
 *      are Copyright 2012-2014 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Brian P. Walenz beginning on 2014-DEC-19
 *      are Copyright 2014-2015 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

static const char *rcsid = "$Id$";

#include "AS_BAT_Datatypes.H"
#include "AS_BAT_BestOverlapGraph.H"
#include "AS_BAT_ChunkGraph.H"

#include "AS_BAT_PopulateUnitig.H"
#include "AS_BAT_PlaceContains.H"

#include "AS_BAT_Unitig.H"


void
extendByMates(UnitigVector &unitigs,
              double        erateGraph) {

  //logFileFlags |= LOG_CHUNK_GRAPH;
  logFileFlags |= LOG_POPULATE_UNITIG;

  writeLog("==> EXTENDING UNITIGS WITH MATE PAIRS.\n");

  uint32 tiMax = unitigs.size();

  for (uint32 ti=0; ti<tiMax; ti++) {
    Unitig        *target = unitigs[ti];

    if (target == NULL)
      continue;

    if (target->ufpath.size() < 2)
      continue;

    //  Build a list of all the fragments in this unitig, and any mates that are not in a unitig.

    uint32        extraMates = 0;

    for (uint32 fi=0; fi<target->ufpath.size(); fi++) {
      uint32  fid = target->ufpath[fi].ident;
      uint32  mid = FI->mateIID(fid);

      if ((mid != 0) &&
          (Unitig::fragIn(mid) == 0))
        extraMates++;
    }

    writeLog("\n");
    writeLog("unitig "F_U32" of size "F_SIZE_T" with "F_U32" extra fragments via mates\n",
            ti, target->ufpath.size(), extraMates);

    if (extraMates == 0)
      continue;

    //  Build a set of the fragments in this unitig plus their mates, and a set of just the mates.

    set<uint32>   frags;
    set<uint32>   mates;

    for (uint32 fi=0; fi<target->ufpath.size(); fi++) {
      uint32  fid = target->ufpath[fi].ident;
      uint32  mid = FI->mateIID(fid);

      frags.insert(fid);

      if ((mid != 0) &&
          (Unitig::fragIn(mid) == 0)) {
        writeLog("  mate frag "F_U32"\n", mid);
        frags.insert(mid);
        mates.insert(mid);
      }
    }

    //  Now, remove all the unitig fragments from the unitig so we can reconstruct it with the
    //  additional mated fragments.  Note that this loop cannot be combined with the last, since
    //  the test for 'additional mate' is 'not in the same unitig' -- and if we remove the
    //  fragments too early, we can't distinguish 'additional' from 'included'.

    for (uint32 fi=0; fi<target->ufpath.size(); fi++)
      target->removeFrag(target->ufpath[fi].ident);

    unitigs[ti] = NULL;
    delete target;

    //  Build a new BOG for just those fragments - in particular, only overlaps within the set are
    //  used for the BOG.

    BestOverlapGraph  *OGsave = OG;
    ChunkGraph        *CGsave = CG;

    OG = new BestOverlapGraph(erateGraph, &frags);
    CG = new ChunkGraph(&frags);

    uint32  numTigs = unitigs.size();

    //  Build new unitigs.  There should only be one new unitig constructed, but that isn't
    //  guaranteed.  No new unitigs are built if they are seeded from the mate fragments.  This
    //  isn't ideal -- we'd like to allow the first unitig (supposedly the longest) to start from
    //  a mate fragment.  However, consider the not-so-rare case where the original unitig is two
    //  backbone fragments and lots of contains.  Those contains contribute mate pairs that all
    //  assemble together, giving a longer path than the original unitig.  We don't want to
    //  assemble the mated fragments yet (we'll wait until we get the rest of the fragments that
    //  could assemble together).

    for (uint32 fi = CG->nextFragByChunkLength(); fi > 0; fi=CG->nextFragByChunkLength()) {
      if ((Unitig::fragIn(fi) != 0) ||
          (mates.count(fi) > 0))
        //  Fragment already in a unitig, or is an additional mate that we don't want
        //  to seed from.
        continue;

      populateUnitig(unitigs, fi);
    }

    //  Report what was constructed

    if (unitigs.size() - numTigs > 1)
      writeLog("WARNING: mate extension split a unitig.\n");


    for (uint32 newTigs=numTigs; newTigs<unitigs.size(); newTigs++) {
      Unitig  *tig = unitigs[newTigs];

      if (tig == NULL)
        continue;

      placeContainsUsingBestOverlaps(tig, &frags);

      writeLog("  new tig "F_U32" with "F_SIZE_T" fragments\n",
              tig->id(), tig->ufpath.size());
    }

    delete OG;
    delete CG;

    OG = OGsave;
    CG = CGsave;
  }
}
