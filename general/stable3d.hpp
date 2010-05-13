// Copyright (c) 2010,  Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
// This file is part of the MFEM library.  See file COPYRIGHT for details.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#ifndef MFEM_STABLE3D
#define MFEM_STABLE3D

#include "mem_alloc.hpp"

class STable3DNode
{
public:
   STable3DNode *Prev;
   int Column, Floor, Number;
};

/// Symmetric 3D Table
class STable3D
{
private:
   int Size, NElem;
   STable3DNode **Rows;

#ifdef MFEM_USE_MEMALLOC
   MemAlloc <STable3DNode, 1024> NodesMem;
#endif

public:
   STable3D (int nr);

   int Push (int r, int c, int f);

   int operator() (int r, int c, int f) const;

   int Index (int r, int c, int f) const;

   int Push4 (int r, int c, int f, int t);

   int operator() (int r, int c, int f, int t) const;

   int NumberOfElements() { return NElem; };

   ~STable3D ();
};

#endif
