// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#include "mesh_extras.hpp"

using namespace std;

namespace mfem
{

namespace miniapps
{

ElementMeshStream::ElementMeshStream(Element::Type e)
{
   *this << "MFEM mesh v1.0" << endl;
   switch (e)
   {
      case Element::SEGMENT:
         *this << "dimension" << endl << 1 << endl
               << "elements" << endl << 1 << endl
               << "1 1 0 1" << endl
               << "boundary" << endl << 2 << endl
               << "1 0 0" << endl
               << "1 0 1" << endl
               << "vertices" << endl
               << 2 << endl
               << 1 << endl
               << 0 << endl
               << 1 << endl;
         break;
      case Element::TRIANGLE:
         *this << "dimension" << endl << 2 << endl
               << "elements" << endl << 1 << endl
               << "1 2 0 1 2" << endl
               << "boundary" << endl << 3 << endl
               << "1 1 0 1" << endl
               << "1 1 1 2" << endl
               << "1 1 2 0" << endl
               << "vertices" << endl
               << "3" << endl
               << "2" << endl
               << "0 0" << endl
               << "1 0" << endl
               << "0 1" << endl;
         break;
      case Element::QUADRILATERAL:
         *this << "dimension" << endl << 2 << endl
               << "elements" << endl << 1 << endl
               << "1 3 0 1 2 3" << endl
               << "boundary" << endl << 4 << endl
               << "1 1 0 1" << endl
               << "1 1 1 2" << endl
               << "1 1 2 3" << endl
               << "1 1 3 0" << endl
               << "vertices" << endl
               << "4" << endl
               << "2" << endl
               << "0 0" << endl
               << "1 0" << endl
               << "1 1" << endl
               << "0 1" << endl;
         break;
      case Element::TETRAHEDRON:
         *this << "dimension" << endl << 3 << endl
               << "elements" << endl << 1 << endl
               << "1 4 0 1 2 3" << endl
               << "boundary" << endl << 4 << endl
               << "1 2 0 2 1" << endl
               << "1 2 1 2 3" << endl
               << "1 2 2 0 3" << endl
               << "1 2 0 1 3" << endl
               << "vertices" << endl
               << "4" << endl
               << "3" << endl
               << "0 0 0" << endl
               << "1 0 0" << endl
               << "0 1 0" << endl
               << "0 0 1" << endl;
         break;
      case Element::HEXAHEDRON:
         *this << "dimension" << endl << 3 << endl
               << "elements" << endl << 1 << endl
               << "1 5 0 1 2 3 4 5 6 7" << endl
               << "boundary" << endl << 6 << endl
               << "1 3 0 3 2 1" << endl
               << "1 3 4 5 6 7" << endl
               << "1 3 0 1 5 4" << endl
               << "1 3 1 2 6 5" << endl
               << "1 3 2 3 7 6" << endl
               << "1 3 3 0 4 7" << endl
               << "vertices" << endl
               << "8" << endl
               << "3" << endl
               << "0 0 0" << endl
               << "1 0 0" << endl
               << "1 1 0" << endl
               << "0 1 0" << endl
               << "0 0 1" << endl
               << "1 0 1" << endl
               << "1 1 1" << endl
               << "0 1 1" << endl;
         break;
      default:
         mfem_error("Invalid element type!");
         break;
   }

}
void
MergeMeshNodes(Mesh * mesh, int logging)
{
   int dim  = mesh->Dimension();
   int sdim = mesh->SpaceDimension();

   double tol = 1.0e-8;
   // double dia = -1.0;

   if ( logging > 0 )
      cout << "Euler Number of Initial Mesh:  "
           << ((dim==3)?mesh->EulerNumber() :
               ((dim==2)?mesh->EulerNumber2D() :
                mesh->GetNV() - mesh->GetNE())) << endl;

   vector<int> v2v(mesh->GetNV());

   Vector vd(sdim);

   for (int i = 0; i < mesh->GetNV(); i++)
   {
      Vector vi(mesh->GetVertex(i), sdim);

      v2v[i] = -1;

      for (int j = 0; j < i; j++)
      {
         Vector vj(mesh->GetVertex(j), sdim);
         add(vi, -1.0, vj, vd);

         if ( vd.Norml2() < tol )
         {
            v2v[i] = j;
            break;
         }
      }
      if ( v2v[i] < 0 ) { v2v[i] = i; }
   }

   // renumber elements
   for (int i = 0; i < mesh->GetNE(); i++)
   {
      Element *el = mesh->GetElement(i);
      int *v = el->GetVertices();
      int nv = el->GetNVertices();
      for (int j = 0; j < nv; j++)
      {
         v[j] = v2v[v[j]];
      }
   }
   // renumber boundary elements
   for (int i = 0; i < mesh->GetNBE(); i++)
   {
      Element *el = mesh->GetBdrElement(i);
      int *v = el->GetVertices();
      int nv = el->GetNVertices();
      for (int j = 0; j < nv; j++)
      {
         v[j] = v2v[v[j]];
      }
   }

   mesh->RemoveUnusedVertices();

   if ( logging > 0 )
   {
      cout << "Euler Number of Final Mesh:    "
           << ((dim==3) ? mesh->EulerNumber() :
               ((dim==2) ? mesh->EulerNumber2D() :
                mesh->GetNV() - mesh->GetNE()))
           << endl;
   }
}

Mesh *
MakePeriodicMesh(Mesh * mesh, const vector<Vector> & trans_vecs, int logging)
{
   int dim  = mesh->Dimension();
   int sdim = mesh->SpaceDimension();

   double tol = 1.0e-8;
   double dia = -1.0;

   if ( logging > 0 )
      cout << "Euler Number of Initial Mesh:  "
           << ((dim==3)?mesh->EulerNumber():mesh->EulerNumber2D()) << endl;

   // map<int,map<int,map<int,int> > > c2v;
   set<int> v;
   set<int>::iterator si, sj, sk;
   map<int,int>::iterator mi;
   map<int,set<int> >::iterator msi;

   Vector coord(NULL, sdim);

   // map<int,vector<double> > bnd_vtx;
   // map<int,vector<double> > shft_bnd_vtx;

   // int d = 5;
   Vector xMax(sdim), xMin(sdim), xDiff(sdim);
   xMax = xMin = xDiff = 0.0;

   for (int be=0; be<mesh->GetNBE(); be++)
   {
      Array<int> dofs;
      mesh->GetBdrElementVertices(be,dofs);

      for (int i=0; i<dofs.Size(); i++)
      {
         v.insert(dofs[i]);

         coord.SetData(mesh->GetVertex(dofs[i]));
         for (int j=0; j<sdim; j++)
         {
            xMax[j] = max(xMax[j],coord[j]);
            xMin[j] = min(xMin[j],coord[j]);
         }
      }
   }
   add(xMax, -1.0, xMin, xDiff);
   dia = xDiff.Norml2();

   if ( logging > 0 )
   {
      cout << "Number of Boundary Vertices:  " << v.size() << endl;

      cout << "xMin: ";
      xMin.Print(cout,sdim);
      cout << "xMax: ";
      xMax.Print(cout,sdim);
      cout << "xDiff: ";
      xDiff.Print(cout,sdim);
   }

   if ( logging > 0 )
   {
      for (si=v.begin(); si!=v.end(); si++)
      {
         cout << *si << ": ";
         coord.SetData(mesh->GetVertex(*si));
         coord.Print(cout);
      }
   }

   map<int,int>        slaves;
   map<int,set<int> > masters;

   for (si=v.begin(); si!=v.end(); si++) { masters[*si]; }

   Vector at(sdim);
   Vector dx(sdim);

   for (unsigned int i=0; i<trans_vecs.size(); i++)
   {
      int c = 0;
      if ( logging > 0 )
      {
         cout << "trans_vecs = ";
         trans_vecs[i].Print(cout,sdim);
      }

      for (si=v.begin(); si!=v.end(); si++)
      {
         coord.SetData(mesh->GetVertex(*si));

         add(coord, trans_vecs[i], at);

         for (sj=v.begin(); sj!=v.end(); sj++)
         {
            coord.SetData(mesh->GetVertex(*sj));
            add(at, -1.0, coord, dx);

            if ( dx.Norml2() > dia * tol )
            {
               continue;
            }

            int master = *si;
            int slave  = *sj;

            bool mInM = masters.find(master) != masters.end();
            bool sInM = masters.find(slave)  != masters.end();

            if ( mInM && sInM )
            {
               // Both vertices are currently masters
               //   Demote "slave" to be a slave of master
               if ( logging > 0 )
               {
                  cout << "Both " << master << " and " << slave
                       << " are masters." << endl;
               }
               masters[master].insert(slave);
               slaves[slave] = master;
               for (sk=masters[slave].begin();
                    sk!=masters[slave].end(); sk++)
               {
                  masters[master].insert(*sk);
                  slaves[*sk] = master;
               }
               masters.erase(slave);
            }
            else if ( mInM && !sInM )
            {
               // "master" is already a master and "slave" is already a slave
               // Make "master" and its slaves slaves of "slave"'s master
               if ( logging > 0 )
               {
                  cout << master << " is already a master and " << slave
                       << " is already a slave of " << slaves[slave]
                       << "." << endl;
               }
               if ( master != slaves[slave] )
               {
                  masters[slaves[slave]].insert(master);
                  slaves[master] = slaves[slave];
                  for (sk=masters[master].begin();
                       sk!=masters[master].end(); sk++)
                  {
                     masters[slaves[slave]].insert(*sk);
                     slaves[*sk] = slaves[slave];
                  }
                  masters.erase(master);
               }
            }
            else if ( !mInM && sInM )
            {
               // "master" is currently a slave and
               // "slave" is currently a master
               // Make "slave" and its slaves slaves of "master"'s master
               if ( logging > 0 )
               {
                  cout << master << " is currently a slave of "
                       << slaves[master]<< " and " << slave
                       << " is currently a master." << endl;
               }
               if ( slave != slaves[master] )
               {
                  masters[slaves[master]].insert(slave);
                  slaves[slave] = slaves[master];
                  for (sk=masters[slave].begin();
                       sk!=masters[slave].end(); sk++)
                  {
                     masters[slaves[master]].insert(*sk);
                     slaves[*sk] = slaves[master];
                  }
                  masters.erase(slave);
               }
            }
            else
            {
               // Both vertices are currently slaves
               // Make "slave" and its fellow slaves slaves
               // of "master"'s master
               if ( logging > 0 )
               {
                  cout << "Both " << master << " and " << slave
                       << " are slaves of " << slaves[master] << " and "
                       << slaves[slave] << " respectively." << endl;
               }

               int master_of_master = slaves[master];
               int master_of_slave  = slaves[slave];

               // Move slave and its fellow slaves to master_of_master
               if ( slaves[master] != slaves[slave] )
               {
                  for (sk=masters[master_of_slave].begin();
                       sk!=masters[master_of_slave].end(); sk++)
                  {
                     masters[master_of_master].insert(*sk);
                     slaves[*sk] = master_of_master;
                  }
                  masters.erase(master_of_slave);
                  slaves[master_of_slave] = master_of_master;
               }
            }
            c++;
            break;
         }
      }
      if ( logging > 0 )
      {
         cout << "Found " << c << " possible node";
         if ( c != 1 ) { cout << "s"; }
         cout <<" to project." << endl;
      }
   }
   if ( logging > 0 )
   {
      cout << "Number of Master Vertices:  " << masters.size() << endl;
      cout << "Number of Slave Vertices:   " << slaves.size() << endl;
      cout << "Master to slave mapping:" << endl;
      for (msi=masters.begin(); msi!=masters.end(); msi++)
      {
         cout << msi->first << " ->";
         for (si=msi->second.begin(); si!=msi->second.end(); si++)
         {
            cout << " " << *si;
         }
         cout << endl;
      }
      cout << "Slave to master mapping:" << endl;
      for (mi=slaves.begin(); mi!=slaves.end(); mi++)
      {
         cout << mi->first << " <- " << mi->second << endl;
      }
   }

   Array<int> v2v(mesh->GetNV());

   for (int i=0; i<v2v.Size(); i++)
   {
      v2v[i] = i;
   }

   for (mi=slaves.begin(); mi!=slaves.end(); mi++)
   {
      v2v[mi->first] = mi->second;
   }

   Mesh *per_mesh = new Mesh(*mesh, true);

   per_mesh->SetCurvature(1, true);

   // renumber elements
   for (int i = 0; i < per_mesh->GetNE(); i++)
   {
      Element *el = per_mesh->GetElement(i);
      int *v = el->GetVertices();
      int nv = el->GetNVertices();
      for (int j = 0; j < nv; j++)
      {
         v[j] = v2v[v[j]];
      }
   }
   // renumber boundary elements
   for (int i = 0; i < per_mesh->GetNBE(); i++)
   {
      Element *el = per_mesh->GetBdrElement(i);
      int *v = el->GetVertices();
      int nv = el->GetNVertices();
      for (int j = 0; j < nv; j++)
      {
         v[j] = v2v[v[j]];
      }
   }

   per_mesh->RemoveUnusedVertices();
   // per_mesh->RemoveInternalBoundaries();

   if ( logging > 0 )
   {
      cout << "Euler Number of Final Mesh:    "
           << ((dim==3)?per_mesh->EulerNumber():per_mesh->EulerNumber2D())
           << endl;
   }
   return per_mesh;
}

} // namespace miniapps

} // namespace mfem
