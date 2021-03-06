
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
 *    src/AS_BAT/AS_BAT_InsertSizes.H
 *
 *  Modifications by:
 *
 *    Brian P. Walenz from 2010-NOV-23 to 2013-AUG-01
 *      are Copyright 2010,2013 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Brian P. Walenz beginning on 2014-DEC-19
 *      are Copyright 2014 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

#ifndef INCLUDE_AS_INSERT_SIZES
#define INCLUDE_AS_INSERT_SIZES

static const char *rcsid_INCLUDE_AS_BAT_INSERTSIZES = "$Id$";

#include "AS_BAT_Datatypes.H"
#include "AS_BAT_Unitig.H"

class
InsertSizes {
public:
  InsertSizes(UnitigVector &unitigs);
  ~InsertSizes();

  int32      mean(uint32 libIID)    { assert(libIID > 0); assert(libIID <= _numLibs); return(_mean[libIID]);          };
  int32      stddev(uint32 libIID)  { assert(libIID > 0); assert(libIID <= _numLibs); return(_stddev[libIID]);        };
  bool       valid(uint32 libIID)   { assert(libIID > 0); assert(libIID <= _numLibs); return(_samples[libIID] >= 10); };

private:
  void       accumulateLibraryStats(Unitig *utg);

  uint32    _numLibs;

  int32   **_dist;
  int32    *_distLen;
  int32    *_distMax;

  int32    *_mean;
  int32    *_stddev;
  int32    *_samples;
};

#endif // INCLUDE_AS_INSERT_SIZES
