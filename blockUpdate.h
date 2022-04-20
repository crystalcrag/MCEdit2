/*
 * blockUpdate.h : public functions to deal with delayed block update
 *
 * Written by T.Pierron, march 2021.
 */

#ifndef MC_BLOCK_UPDATE
#define MC_BLOCK_UPDATE

#include "maps.h"

int blockRotateX90(int blockId);
int blockRotateY90(int blockId);
int blockRotateZ90(int blockId);
int blockMirrorX(BlockIter);
int blockMirrorY(BlockIter);
int blockMirrorZ(BlockIter);

#endif
