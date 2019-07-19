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

#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <complex>

using namespace std;
using namespace mfem;

// #define DEFINITE

#ifndef MFEM_USE_PETSC
#error This example requires that MFEM is built with MFEM_USE_PETSC=YES
#endif

// Define exact solution
void E_exact_Re(const Vector & x, Vector & E);
void f_exact_Re(const Vector & x, Vector & f);
void get_maxwell_solution_Re(const Vector & x, double E[], double curl2E[]);

void E_exact_Im(const Vector & x, Vector & E);
void f_exact_Im(const Vector & x, Vector & f);
void get_maxwell_solution_Im(const Vector & x, double E[], double curl2E[]);


// Mesh Size
Vector mesh_dim_(0); // x, y, z dimensions of mesh
int dim;
double omega;
double complex_shift;
int isol = 1;


int main(int argc, char *argv[])
{
   StopWatch chrono;

   // 1. Initialise MPI
   int num_procs, myid;
   MPI_Init(&argc, &argv); // Initialise MPI
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs); //total number of processors available
   MPI_Comm_rank(MPI_COMM_WORLD, &myid); // Determine process identifier

   const char *mesh_file = "../../data/one-hex.mesh";
   int order = 1;
   // number of wavelengths
   double k = 0.5;
   //
   const char *petscrc_file = "petscrc_mult_options";
   // visualization flag
   bool visualization = 1;
   // number of initial ref
   int initref = 1;
   // number of mg levels
   int maxref = 1;
   //
   complex_shift = 0.0;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree)."); 
   args.AddOption(&k, "-k", "--wavelengths",
                  "Number of wavelengths"); 
   args.AddOption(&complex_shift, "-cs", "--complex_shift",
                  "Complex shift");                
   args.AddOption(&isol, "-isol", "--exact",
                  "Exact solution flag - 0:polynomial, 1: plane wave");
   args.AddOption(&initref, "-initref", "--initref",
                  "Number of initial refinements.");
   args.AddOption(&maxref, "-maxref", "--maxref",
                  "Number of Refinements.");   
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");                           
   args.Parse();
   // check if the inputs are correct
   if (!args.Good())
   {
      if (myid == 0)
      {
         args.PrintUsage(cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (myid == 0)
   {
      args.PrintOptions(cout);
   }

   // Angular frequency
   omega = 2.0*k*M_PI;

// 2b. We initialize PETSc
   MFEMInitializePetsc(NULL, NULL, petscrc_file, NULL);

   // Create serial mesh 
   Mesh *mesh = new Mesh(mesh_file, 1, 1);
   dim = mesh->Dimension();
   int sdim = mesh->SpaceDimension();

   // 3. Executing uniform h-refinement
   for (int i = 0; i < initref; i++ )
   {
      mesh->UniformRefinement();
   }

   // create parallel mesh and delete the serial one
   ParMesh *pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);
   delete mesh;

   // Create H(curl) (Nedelec) Finite element space
   FiniteElementCollection *fec   = new ND_FECollection(order, dim);
   ParFiniteElementSpace *ND_fespace = new ParFiniteElementSpace(pmesh, fec);

   std::vector<HypreParMatrix*>  P(maxref);

   for (int i = 0; i < maxref; i++)
   {
      const ParFiniteElementSpace cfespace(*ND_fespace);
      pmesh->UniformRefinement();
      // Update fespace
      ND_fespace->Update();
      OperatorHandle Tr(Operator::Hypre_ParCSR);
      ND_fespace->GetTrueTransferOperator(cfespace, Tr);
      Tr.SetOperatorOwner(false);
      Tr.Get(P[i]);
   }

   // 7. Linear form b(.) (Right hand side)
   VectorFunctionCoefficient f_Re(dim, f_exact_Re);
   VectorFunctionCoefficient f_Im(dim, f_exact_Im);
   ParComplexLinearForm b(ND_fespace,ComplexOperator::HERMITIAN);
   b.AddDomainIntegrator(new VectorFEDomainLFIntegrator(f_Re), 
                         new VectorFEDomainLFIntegrator(f_Im));
   b.real().Vector::operator=(0.0);
   b.imag().Vector::operator=(0.0);
   b.Assemble();


   // 7. Bilinear form a(.,.) on the finite element space
   ConstantCoefficient muinv(1.0);
   ConstantCoefficient sigma(-pow(omega, 2));
   ConstantCoefficient alpha(complex_shift);
   ParSesquilinearForm a(ND_fespace, ComplexOperator::HERMITIAN);
   a.AddDomainIntegrator(new CurlCurlIntegrator(muinv),NULL); 
   a.AddDomainIntegrator(new VectorFEMassIntegrator(sigma),NULL);
   a.AddDomainIntegrator(NULL,new VectorFEMassIntegrator(alpha));
   a.Assemble();
   a.Finalize();


   Array<int> ess_tdof_list;
   if (pmesh->bdr_attributes.Size())
   {
      Array<int> ess_bdr(pmesh->bdr_attributes.Max());
      ess_bdr = 1;
      ND_fespace->GetEssentialTrueDofs(ess_bdr, ess_tdof_list);
   }

   // Solution grid function
   ParComplexGridFunction E_gf(ND_fespace);
   VectorFunctionCoefficient E_Re(dim, E_exact_Re);
   VectorFunctionCoefficient E_Im(dim, E_exact_Im);
   E_gf.ProjectCoefficient(E_Re,E_Im);

   OperatorHandle Ah;
   Vector X, B;
   a.FormLinearSystem(ess_tdof_list, E_gf, b, Ah, X, B);

   ComplexHypreParMatrix * AZ = Ah.As<ComplexHypreParMatrix>();
   HypreParMatrix * A = AZ->GetSystemMatrix();

   if (myid == 0)
   {
      cout << "Size of fine grid system: "
           << A->GetGlobalNumRows() << " x " << A->GetGlobalNumCols() << endl;
   }


   ComplexGMGSolver M(AZ, P);
   M.SetTheta(0.5);
   M.SetSmootherType(HypreSmoother::Jacobi);

   int maxit(5000);
   double rtol(1.e-12);
   double atol(0.0);

   X = 0.0;
   GMRESSolver gmres(MPI_COMM_WORLD);
   gmres.SetAbsTol(atol);
   gmres.SetRelTol(rtol);
   gmres.SetMaxIter(maxit);
   gmres.SetOperator(*AZ);
   gmres.SetPreconditioner(M);
   gmres.SetPrintLevel(1);
   gmres.Mult(B, X);


   // PetscLinearSolver * invA = new PetscLinearSolver(MPI_COMM_WORLD, "direct");
   // invA->SetOperator(PetscParMatrix(A, Operator::PETSC_MATAIJ));
   // invA->Mult(B,X);



   a.RecoverFEMSolution(X,B,E_gf);


   // Compute error
   int order_quad = max(2, 2 * order + 1);
   const IntegrationRule *irs[Geometry::NumGeom];
   for (int i = 0; i < Geometry::NumGeom; ++i)
   {
      irs[i] = &(IntRules.Get(i, order_quad));
   }

   double L2Error_Re = E_gf.real().ComputeL2Error(E_Re, irs);
   double norm_E_Re = ComputeGlobalLpNorm(2, E_Re, *pmesh, irs);

   double L2Error_Im = E_gf.imag().ComputeL2Error(E_Im, irs);
   double norm_E_Im = ComputeGlobalLpNorm(2, E_Im, *pmesh, irs);


   if (myid == 0)
   {
      cout << " Real Part: || E_h - E || / ||E|| = " << L2Error_Re / norm_E_Re << '\n' << endl;
      cout << " Imag Part: || E_h - E || / ||E|| = " << L2Error_Im / norm_E_Im << '\n' << endl;

      cout << " Real Part: || E_h - E || = " << L2Error_Re << '\n' << endl;
      cout << " Imag Part: || E_h - E || = " << L2Error_Im << '\n' << endl;
   }


   // visualization   
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock(vishost, visport);
      sol_sock << "parallel " << num_procs << " " << myid << "\n";
      sol_sock.precision(8);
      sol_sock << "solution\n" << *pmesh << E_gf.real() << "window_title 'Real part'" << flush;

      socketstream sol_sock_Im(vishost, visport);
      sol_sock_Im << "parallel " << num_procs << " " << myid << "\n";
      sol_sock_Im.precision(8);
      sol_sock_Im << "solution\n" << *pmesh << E_gf.imag() << "window_title 'Imaginary part'" << flush;
   }

   // delete invA;
   delete fec;
   delete ND_fespace;
   delete pmesh;

   MFEMFinalizePetsc();
   MPI_Finalize();

}
//define exact solution
void E_exact_Re(const Vector &x, Vector &E)
{
   double curl2E[3];
   get_maxwell_solution_Re(x, E, curl2E);
}

//calculate RHS from exact solution
void f_exact_Re(const Vector &x, Vector &f)
{
   double E_Re[3], curl2E_Re[3];
   double E_Im[3], curl2E_Im[3];

   get_maxwell_solution_Re(x, E_Re, curl2E_Re);
   get_maxwell_solution_Re(x, E_Im, curl2E_Im);

   // curl ( curl E) - omega^2 E = f
   double coeff;
   coeff = -omega * omega;
   f(0) = curl2E_Re[0] + coeff * E_Re[0];
   f(1) = curl2E_Re[1] + coeff * E_Re[1];
   f(2) = curl2E_Re[2] + coeff * E_Re[2];

   // Acount for the complex shift
   f(0) += -complex_shift*E_Im[0];
   f(1) += -complex_shift*E_Im[1];
   f(2) += -complex_shift*E_Im[2];
}

void get_maxwell_solution_Re(const Vector & x, double E[], double curl2E[])
{

   if (isol == 0) // polynomial
   {
      E[0] = x[1] * x[2]      * (1.0 - x[1]) * (1.0 - x[2]);
      E[1] = x[0] * x[1] * x[2] * (1.0 - x[0]) * (1.0 - x[2]);
      E[2] = x[0] * x[1]      * (1.0 - x[0]) * (1.0 - x[1]);
      curl2E[0] = 2.0 * x[1] * (1.0 - x[1]) - (2.0 * x[0] - 3.0) * x[2] * (1 - x[2]);
      curl2E[1] = 2.0 * x[1] * (x[0] * (1.0 - x[0]) + (1.0 - x[2]) * x[2]);
      curl2E[2] = 2.0 * x[1] * (1.0 - x[1]) + x[0] * (3.0 - 2.0 * x[2]) * (1.0 - x[0]);
   }
   else
   {
      double alpha = omega / sqrt(3);
      E[0] = cos(alpha*(x(0) + x(1) + x(2)));
      E[1] = 0.0;
      E[2] = 0.0;

      curl2E[0] = 2.0 * alpha * alpha * E[0];
      curl2E[1] = -alpha * alpha * E[0];
      curl2E[2] = -alpha * alpha * E[0];
   }
}



//define exact solution
void E_exact_Im(const Vector &x, Vector &E)
{
   double curl2E[3];
   get_maxwell_solution_Re(x, E, curl2E);
}

//calculate RHS from exact solution
void f_exact_Im(const Vector &x, Vector &f)
{
   double E_Re[3], curl2E_Re[3];
   double E_Im[3], curl2E_Im[3];

   get_maxwell_solution_Re(x, E_Im, curl2E_Im);
   get_maxwell_solution_Re(x, E_Re, curl2E_Re);

   // curl ( curl E) - omega^2 E = f
   double coeff;
   coeff = -omega * omega;
   f(0) = curl2E_Im[0] + coeff * E_Im[0];
   f(1) = curl2E_Im[1] + coeff * E_Im[1];
   f(2) = curl2E_Im[2] + coeff * E_Im[2];

   // Acount for the complex shift
   f(0) += complex_shift*E_Re[0];
   f(1) += complex_shift*E_Re[1];
   f(2) += complex_shift*E_Re[2];
}

void get_maxwell_solution_Im(const Vector & x, double E[], double curl2E[])
{
   if (isol == 0) // polynomial
   {
      E[0] = x[1] * x[2]      * (1.0 - x[1]) * (1.0 - x[2]);
      E[1] = x[0] * x[1] * x[2] * (1.0 - x[0]) * (1.0 - x[2]);
      E[2] = x[0] * x[1]      * (1.0 - x[0]) * (1.0 - x[1]);
      curl2E[0] = 2.0 * x[1] * (1.0 - x[1]) - (2.0 * x[0] - 3.0) * x[2] * (1 - x[2]);
      curl2E[1] = 2.0 * x[1] * (x[0] * (1.0 - x[0]) + (1.0 - x[2]) * x[2]);
      curl2E[2] = 2.0 * x[1] * (1.0 - x[1]) + x[0] * (3.0 - 2.0 * x[2]) * (1.0 - x[0]);
   }
   else
   {
      double alpha = omega / sqrt(3);   
      E[0] = sin(alpha * (x(0) + x(1) + x(2)));
      E[1] = 0.0;
      E[2] = 0.0;
      curl2E[0] = 2.0 * alpha * alpha * E[0];
      curl2E[1] = -alpha * alpha * E[0];
      curl2E[2] = -alpha * alpha * E[0];
   }   
}