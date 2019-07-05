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

#include "cold_plasma_dielectric_solver.hpp"

#ifdef MFEM_USE_MPI

using namespace std;

namespace mfem
{
using namespace miniapps;

namespace plasma
{

// Used for combining scalar coefficients
double prodFunc(double a, double b) { return a * b; }

CPDSolver::CPDSolver(ParMesh & pmesh, int order, double omega,
                     CPDSolver::SolverType sol, SolverOptions & sOpts,
                     CPDSolver::PrecondType prec,
                     ComplexOperator::Convention conv,
                     MatrixCoefficient & epsReCoef,
                     MatrixCoefficient & epsImCoef,
                     MatrixCoefficient & epsAbsCoef,
                     Coefficient & muInvCoef,
                     Coefficient * etaInvCoef,
                     VectorCoefficient * kCoef,
                     Array<int> & abcs,
                     Array<int> & dbcs,
                     // void   (*e_r_bc )(const Vector&, Vector&),
                     // void   (*e_i_bc )(const Vector&, Vector&),
                     VectorCoefficient & EReCoef,
                     VectorCoefficient & EImCoef,
                     void   (*j_r_src)(const Vector&, Vector&),
                     void   (*j_i_src)(const Vector&, Vector&))
   : myid_(0),
     num_procs_(1),
     order_(order),
     logging_(1),
     sol_(sol),
     solOpts_(sOpts),
     prec_(prec),
     conv_(conv),
     ownsEtaInv_(etaInvCoef == NULL),
     omega_(omega),
     solNorm_(-1.0),
     pmesh_(&pmesh),
     L2VFESpace_(NULL),
     HCurlFESpace_(NULL),
     a1_(NULL),
     b1_(NULL),
     e_(NULL),
     j_(NULL),
     rhs_(NULL),
     e_t_(NULL),
     e_v_(NULL),
     j_v_(NULL),
     epsReCoef_(&epsReCoef),
     epsImCoef_(&epsImCoef),
     epsAbsCoef_(&epsAbsCoef),
     muInvCoef_(&muInvCoef),
     etaInvCoef_(etaInvCoef),
     kCoef_(kCoef),
     omegaCoef_(new ConstantCoefficient(omega_)),
     negOmegaCoef_(new ConstantCoefficient(-omega_)),
     omega2Coef_(new ConstantCoefficient(pow(omega_, 2))),
     negOmega2Coef_(new ConstantCoefficient(-pow(omega_, 2))),
     abcCoef_(NULL),
     sinkx_(NULL),
     coskx_(NULL),
     negsinkx_(NULL),
     negMuInvCoef_(NULL),
     massReCoef_(NULL),
     massImCoef_(NULL),
     posMassCoef_(NULL),
     negMuInvkxkxCoef_(NULL),
     negMuInvkCoef_(NULL),
     jrCoef_(NULL),
     jiCoef_(NULL),
     rhsrCoef_(NULL),
     rhsiCoef_(NULL),
     erCoef_(EReCoef),
     eiCoef_(EImCoef),
     j_r_src_(j_r_src),
     j_i_src_(j_i_src),
     // e_r_bc_(e_r_bc),
     // e_i_bc_(e_i_bc),
     dbcs_(&dbcs),
     visit_dc_(NULL)
{
   // Initialize MPI variables
   MPI_Comm_size(pmesh_->GetComm(), &num_procs_);
   MPI_Comm_rank(pmesh_->GetComm(), &myid_);

   // Define compatible parallel finite element spaces on the parallel
   // mesh. Here we use arbitrary order H1, Nedelec, and Raviart-Thomas finite
   // elements.
   // H1FESpace_    = new H1_ParFESpace(pmesh_,order,pmesh_->Dimension());
   HCurlFESpace_ = new ND_ParFESpace(pmesh_,order,pmesh_->Dimension());

   if (kCoef_)
   {
      L2VFESpace_ = new L2_ParFESpace(pmesh_,order,pmesh_->Dimension(),
                                      pmesh_->SpaceDimension());
      e_t_ = new ParGridFunction(L2VFESpace_);
      e_v_ = new ParComplexGridFunction(L2VFESpace_);
      j_v_ = new ParComplexGridFunction(L2VFESpace_);

      sinkx_ = new PhaseCoefficient(*kCoef_, &sin);
      coskx_ = new PhaseCoefficient(*kCoef_, &cos);
      negsinkx_ = new ProductCoefficient(-1.0, *sinkx_);

      negMuInvCoef_ = new ProductCoefficient(-1.0, *muInvCoef_);
      negMuInvkCoef_ = new ScalarVectorProductCoefficient(*negMuInvCoef_,
                                                          *kCoef_);
      negMuInvkxkxCoef_ = new CrossCrossCoefficient(*muInvCoef_, *kCoef_);
   }
   else
   {
      e_t_ = new ParGridFunction(HCurlFESpace_);
   }

   // HDivFESpace_  = new RT_ParFESpace(pmesh_,order,pmesh_->Dimension());
   if (false)
   {
      GridFunction * nodes = pmesh_->GetNodes();
      cout << "nodes is " << nodes << endl;
      for (int i=0; i<HCurlFESpace_->GetNBE(); i++)
      {
         const FiniteElement &be = *HCurlFESpace_->GetBE(i);
         ElementTransformation *eltrans = HCurlFESpace_->GetBdrElementTransformation (i);
         cout << i << '\t' << pmesh_->GetBdrAttribute(i)
              << '\t' << be.GetGeomType()
              << '\t' << eltrans->ElementNo
              << '\t' << eltrans->Attribute
              << endl;
      }
   }

   blockTrueOffsets_.SetSize(3);
   blockTrueOffsets_[0] = 0;
   blockTrueOffsets_[1] = HCurlFESpace_->TrueVSize();
   blockTrueOffsets_[2] = HCurlFESpace_->TrueVSize();
   blockTrueOffsets_.PartialSum();

   // int irOrder = H1FESpace_->GetElementTransformation(0)->OrderW()
   //            + 2 * order;
   // int geom = H1FESpace_->GetFE(0)->GetGeomType();
   // const IntegrationRule * ir = &IntRules.Get(geom, irOrder);
   /*
   // Select surface attributes for Dirichlet BCs
   ess_bdr_.SetSize(pmesh.bdr_attributes.Max());
   non_k_bdr_.SetSize(pmesh.bdr_attributes.Max());
   ess_bdr_ = 1;   // All outer surfaces
   non_k_bdr_ = 1; // Surfaces without applied surface currents
   for (int i=0; i<kbcs.Size(); i++)
   {
      non_k_bdr_[kbcs[i]-1] = 0;
   }
   */
   ess_bdr_.SetSize(pmesh.bdr_attributes.Max());
   if ( dbcs_ != NULL )
   {
      if ( dbcs_->Size() == 1 && (*dbcs_)[0] == -1 )
      {
         ess_bdr_ = 1;
      }
      else
      {
         ess_bdr_ = 0;
         for (int i=0; i<dbcs_->Size(); i++)
         {
            ess_bdr_[(*dbcs_)[i]-1] = 1;
         }
      }
      HCurlFESpace_->GetEssentialTrueDofs(ess_bdr_, ess_bdr_tdofs_);
   }
   // Setup various coefficients
   /*
   // Vector Potential on the outer surface
   if ( a_bc_ == NULL )
   {
      Vector Zero(3);
      Zero = 0.0;
      aBCCoef_ = new VectorConstantCoefficient(Zero);
   }
   else
   {
      aBCCoef_ = new VectorFunctionCoefficient(pmesh_->SpaceDimension(),
                                               *a_bc_);
   }
   */
   massReCoef_ = new ScalarMatrixProductCoefficient(*negOmega2Coef_,
                                                    *epsReCoef_);
   massImCoef_ = new ScalarMatrixProductCoefficient(*negOmega2Coef_,
                                                    *epsImCoef_);
   posMassCoef_ = new ScalarMatrixProductCoefficient(*omega2Coef_,
                                                     *epsAbsCoef_);

   // Impedance of free space
   if ( abcs.Size() > 0 )
   {
      if ( myid_ == 0 && logging_ > 0 )
      {
         cout << "Creating Admittance Coefficient" << endl;
      }

      abc_marker_.SetSize(pmesh.bdr_attributes.Max());
      if ( abcs.Size() == 1 && abcs[0] < 0 )
      {
         // Mark all boundaries as absorbing
         abc_marker_ = 1;
      }
      else
      {
         // Mark select boundaries as absorbing
         abc_marker_ = 0;
         for (int i=0; i<abcs.Size(); i++)
         {
            abc_marker_[abcs[i]-1] = 1;
         }
      }
      if ( etaInvCoef_ == NULL )
      {
         etaInvCoef_ = new ConstantCoefficient(sqrt(epsilon0_/mu0_));
      }
      abcCoef_ = new TransformedCoefficient(negOmegaCoef_, etaInvCoef_,
                                            prodFunc);
   }

   // Volume Current Density
   if ( j_r_src_ != NULL )
   {
      jrCoef_ = new VectorFunctionCoefficient(pmesh_->SpaceDimension(),
                                              j_r_src_);
   }
   else
   {
      Vector j(3); j = 0.0;
      jrCoef_ = new VectorConstantCoefficient(j);
   }
   if ( j_i_src_ != NULL )
   {
      jiCoef_ = new VectorFunctionCoefficient(pmesh_->SpaceDimension(),
                                              j_i_src_);
   }
   else
   {
      Vector j(3); j = 0.0;
      jiCoef_ = new VectorConstantCoefficient(j);
   }
   rhsrCoef_ = new ScalarVectorProductCoefficient(omega_, *jiCoef_);
   rhsiCoef_ = new ScalarVectorProductCoefficient(-omega_, *jrCoef_);
   /*
   // Magnetization
   if ( m_src_ != NULL )
   {
      mCoef_ = new VectorFunctionCoefficient(pmesh_->SpaceDimension(),
                                             m_src_);
   }
   */
   // Bilinear Forms
   a1_ = new ParSesquilinearForm(HCurlFESpace_, conv_);
   a1_->AddDomainIntegrator(new CurlCurlIntegrator(*muInvCoef_), NULL);
   a1_->AddDomainIntegrator(new VectorFEMassIntegrator(*massReCoef_),
                            new VectorFEMassIntegrator(*massImCoef_));
   if ( kCoef_)
   {
      a1_->AddDomainIntegrator(new VectorFEMassIntegrator(*negMuInvkxkxCoef_),
                               NULL);
      a1_->AddDomainIntegrator(NULL,
                               new MixedCrossCurlIntegrator(*negMuInvkCoef_));
      a1_->AddDomainIntegrator(NULL,
                               new MixedWeakCurlCrossIntegrator(*negMuInvkCoef_));
   }
   if ( abcCoef_ )
   {
      a1_->AddBoundaryIntegrator(NULL, new VectorFEMassIntegrator(*abcCoef_),
                                 abc_marker_);
   }

   b1_ = new ParBilinearForm(HCurlFESpace_);
   b1_->AddDomainIntegrator(new CurlCurlIntegrator(*muInvCoef_));
   // b1_->AddDomainIntegrator(new VectorFEMassIntegrator(*epsAbsCoef_));
   b1_->AddDomainIntegrator(new VectorFEMassIntegrator(*posMassCoef_));
   //b1_->AddDomainIntegrator(new VectorFEMassIntegrator(*massImCoef_));

   // Build grid functions
   e_  = new ParComplexGridFunction(HCurlFESpace_);
   *e_ = 0.0;
   solNorm_ = e_->ComputeL2Error(const_cast<VectorCoefficient&>(erCoef_),
                                 const_cast<VectorCoefficient&>(eiCoef_));

   j_ = new ParComplexGridFunction(HCurlFESpace_);
   j_->ProjectCoefficient(*jrCoef_, *jiCoef_);

   rhs_ = new ParComplexLinearForm(HCurlFESpace_, conv_);
   rhs_->AddDomainIntegrator(new VectorFEDomainLFIntegrator(*rhsrCoef_),
                             new VectorFEDomainLFIntegrator(*rhsiCoef_));
   rhs_->real().Vector::operator=(0.0);
   rhs_->imag().Vector::operator=(0.0);
}

CPDSolver::~CPDSolver()
{
   delete negMuInvkxkxCoef_;
   delete negMuInvkCoef_;
   delete negMuInvCoef_;
   delete negsinkx_;
   delete coskx_;
   delete sinkx_;
   delete rhsrCoef_;
   delete rhsiCoef_;
   delete jrCoef_;
   delete jiCoef_;
   // delete erCoef_;
   // delete eiCoef_;
   delete massReCoef_;
   delete massImCoef_;
   delete posMassCoef_;
   delete abcCoef_;
   if ( ownsEtaInv_ ) { delete etaInvCoef_; }
   delete omegaCoef_;
   delete negOmegaCoef_;
   delete omega2Coef_;
   delete negOmega2Coef_;

   // delete DivFreeProj_;
   // delete SurfCur_;

   if (e_v_ != e_) { delete e_v_; }
   if (j_v_ != j_) { delete j_v_; }
   // delete e_r_;
   // delete e_i_;
   delete e_;
   // delete b_;
   // delete h_;
   delete j_;
   // delete j_r_;
   // delete j_i_;
   // delete j_;
   // delete k_;
   // delete m_;
   // delete bd_;
   delete rhs_;
   delete e_t_;
   // delete jd_r_;
   // delete jd_i_;
   // delete grad_;
   // delete curl_;

   delete a1_;
   delete b1_;
   // delete curlMuInvCurl_;
   // delete hCurlMass_;
   // delete hDivHCurlMuInv_;
   // delete weakCurlMuInv_;

   delete L2VFESpace_;
   // delete H1FESpace_;
   delete HCurlFESpace_;
   // delete HDivFESpace_;

   map<string,socketstream*>::iterator mit;
   for (mit=socks_.begin(); mit!=socks_.end(); mit++)
   {
      delete mit->second;
   }
}

HYPRE_Int
CPDSolver::GetProblemSize()
{
   return 2 * HCurlFESpace_->GlobalTrueVSize();
}

void
CPDSolver::PrintSizes()
{
   // HYPRE_Int size_h1 = H1FESpace_->GlobalTrueVSize();
   HYPRE_Int size_nd = HCurlFESpace_->GlobalTrueVSize();
   // HYPRE_Int size_rt = HDivFESpace_->GlobalTrueVSize();
   if (myid_ == 0)
   {
      // cout << "Number of H1      unknowns: " << size_h1 << endl;
      cout << "Number of H(Curl) unknowns: " << size_nd << endl;
      // cout << "Number of H(Div)  unknowns: " << size_rt << endl;
   }
}

void
CPDSolver::Assemble()
{
   if ( myid_ == 0 && logging_ > 0 ) { cout << "Assembling ..." << flush; }

   // a0_->Assemble();
   // a0_->Finalize();

   a1_->Assemble();
   a1_->Finalize();

   b1_->Assemble();
   b1_->Finalize();

   rhs_->Assemble();
   /*
   curlMuInvCurl_->Assemble();
   curlMuInvCurl_->Finalize();
   hDivHCurlMuInv_->Assemble();
   hDivHCurlMuInv_->Finalize();
   hCurlMass_->Assemble();
   hCurlMass_->Finalize();
   curl_->Assemble();
   curl_->Finalize();
   if ( grad_ )
   {
      grad_->Assemble();
      grad_->Finalize();
   }
   if ( weakCurlMuInv_ )
   {
      weakCurlMuInv_->Assemble();
      weakCurlMuInv_->Finalize();
   }
   */
   if ( myid_ == 0 && logging_ > 0 ) { cout << " done." << endl; }
}

void
CPDSolver::Update()
{
   if ( myid_ == 0 && logging_ > 0 ) { cout << "Updating ..." << endl; }

   // Inform the spaces that the mesh has changed
   // Note: we don't need to interpolate any GridFunctions on the new mesh
   // so we pass 'false' to skip creation of any transformation matrices.
   // H1FESpace_->Update(false);
   HCurlFESpace_->Update();
   // HDivFESpace_->Update(false);

   if ( ess_bdr_.Size() > 0 )
   {
      HCurlFESpace_->GetEssentialTrueDofs(ess_bdr_, ess_bdr_tdofs_);
   }

   blockTrueOffsets_[0] = 0;
   blockTrueOffsets_[1] = HCurlFESpace_->TrueVSize();
   blockTrueOffsets_[2] = HCurlFESpace_->TrueVSize();
   blockTrueOffsets_.PartialSum();

   // Inform the grid functions that the space has changed.
   e_->Update();
   // e_r_->Update();
   // e_i_->Update();
   // h_->Update();
   // b_->Update();
   // bd_->Update();
   rhs_->Update();
   // jd_i_->Update();
   // if ( jr_ ) { jr_->Update(); }
   if ( j_  ) {  j_->Update(); }
   // if ( j_r_  ) {  j_r_->Update(); }
   // if ( j_i_  ) {  j_i_->Update(); }
   // if ( k_  ) {  k_->Update(); }
   // if ( m_  ) {  m_->Update(); }

   // Inform the bilinear forms that the space has changed.
   // a0_->Update();
   a1_->Update();
   b1_->Update();
   // curlMuInvCurl_->Update();
   // hCurlMass_->Update();
   // hDivHCurlMuInv_->Update();
   // if ( weakCurlMuInv_ ) { weakCurlMuInv_->Update(); }

   // Inform the other objects that the space has changed.
   // curl_->Update();
   // if ( grad_        ) { grad_->Update(); }
   // if ( DivFreeProj_ ) { DivFreeProj_->Update(); }
   // if ( SurfCur_     ) { SurfCur_->Update(); }
}

void
CPDSolver::Solve()
{
   if ( myid_ == 0 && logging_ > 0 ) { cout << "Running solver ... " << endl; }

   // *e_ = 0.0;

   /// For testing
   // e_->ProjectCoefficient(*jrCoef_, *jiCoef_);
   /*
   ComplexHypreParMatrix * A1 =
      a1_->ParallelAssemble();
   if ( A1->hasRealPart() ) { A1->real().Print("A1_real.mat"); }
   if ( A1->hasImagPart() ) { A1->imag().Print("A1_imag.mat"); }
   HypreParMatrix * A1C = A1->GetSystemMatrix();
   A1C->Print("A1_combined.mat");
   HypreParVector ra(*A1C); ra.Randomize(123);
   HypreParVector A1ra(*A1C);
   HypreParVector Ara(*A1C);
   HypreParVector diff(*A1C);
   A1->Mult(ra, A1ra);
   A1C->Mult(ra, Ara);
   subtract(A1ra, Ara, diff);
   ra.Print("r.vec");
   A1ra.Print("A1r.vec");
   Ara.Print("Ar.vec");
   diff.Print("diff.vec");
   double nrm = Ara.Norml2();
   double nrm1 = A1ra.Norml2();
   double nrmdiff = diff.Norml2();
   if ( myid_ == 0 )
   {
      cout << "norms " << nrm << " " << nrm1 << " " << nrmdiff << endl;
   }
   */
   /*
   HYPRE_Int size = HCurlFESpace_->GetTrueVSize();
   Vector E(2*size), RHS(2*size);
   jd_->ParallelAssemble(RHS);
   e_->ParallelProject(E);
   */
   OperatorHandle A1;
   Vector E, RHS;
   // cout << "Norm of jd (pre-fls): " << jd_->Norml2() << endl;
   e_->ProjectCoefficient(const_cast<VectorCoefficient&>(erCoef_),
                          const_cast<VectorCoefficient&>(eiCoef_));
   a1_->FormLinearSystem(ess_bdr_tdofs_, *e_, *rhs_, A1, E, RHS);

   // cout << "Norm of jd (post-fls): " << jd_->Norml2() << endl;
   // cout << "Norm of RHS: " << RHS.Norml2() << endl;

   OperatorHandle PCOp;
   b1_->FormSystemMatrix(ess_bdr_tdofs_, PCOp);

   /*
   #ifdef MFEM_USE_SUPERLU
   SuperLURowLocMatrix A_SuperLU(*A1C);
   SuperLUSolver solver(MPI_COMM_WORLD);
   solver.SetOperator(A_SuperLU);
   solver.Mult(RHS, E);
   #endif
   #ifdef MFEM_USE_STRUMPACK
   STRUMPACKRowLocMatrix A_STRUMPACK(*A1C);
   STRUMPACKSolver solver(0, NULL, MPI_COMM_WORLD);
   solver.SetOperator(A_STRUMPACK);
   solver.Mult(RHS, E);
   #endif
   */
   /*
   MINRESSolver minres(HCurlFESpace_->GetComm());
   minres.SetOperator(*A1);
   minres.SetRelTol(1e-6);
   minres.SetMaxIter(5000);
   minres.SetPrintLevel(1);
   // pcg.SetPreconditioner(ams);
   minres.Mult(RHS, E);
   */
   tic_toc.Clear();
   tic_toc.Start();
   
   Operator * pcr = NULL;
   Operator * pci = NULL;
   BlockDiagonalPreconditioner * BDP = NULL;

   if (sol_ == FGMRES || sol_ == MINRES)
   {
      switch (prec_)
      {
         case INVALID_PC:
            if ( myid_ == 0 && logging_ > 0 )
            {
               cout << "No Preconditioner Requested" << endl;
            }
            break;
         case DIAG_SCALE:
            if ( myid_ == 0 && logging_ > 0 )
            {
               cout << "Diagonal Scaling Preconditioner Requested" << endl;
            }
            pcr = new HypreDiagScale(dynamic_cast<HypreParMatrix&>(*PCOp.Ptr()));
            break;
         case PARASAILS:
            if ( myid_ == 0 && logging_ > 0 )
            {
               cout << "ParaSails Preconditioner Requested" << endl;
            }
            pcr = new HypreParaSails(dynamic_cast<HypreParMatrix&>(*PCOp.Ptr()));
            dynamic_cast<HypreParaSails*>(pcr)->SetSymmetry(1);
            break;
         case EUCLID:
            if ( myid_ == 0 && logging_ > 0 )
            {
               cout << "Euclid Preconditioner Requested" << endl;
            }
            pcr = new HypreEuclid(dynamic_cast<HypreParMatrix&>(*PCOp.Ptr()));
            if (solOpts_.euLvl != 1)
            {
               HypreSolver * pc = dynamic_cast<HypreSolver*>(pcr);
               HYPRE_EuclidSetLevel(*pc, solOpts_.euLvl);
            }
            break;
         case AMS:
            if ( myid_ == 0 && logging_ > 0 )
            {
               cout << "AMS Preconditioner Requested" << endl;
            }
            pcr = new HypreAMS(dynamic_cast<HypreParMatrix&>(*PCOp.Ptr()),
                               HCurlFESpace_);
            break;
         default:
            MFEM_ABORT("Requested preconditioner is not available.");
            break;
      }
      if (pcr && conv_ != ComplexOperator::HERMITIAN)
      {
         pci = new ScaledOperator(pcr, -1.0);
      }
      else
      {
         pci = pcr;
      }

      if (pcr)
      {
         BDP = new BlockDiagonalPreconditioner(blockTrueOffsets_);
         BDP->SetDiagonalBlock(0, pcr);
         BDP->SetDiagonalBlock(1, pci);
         BDP->owns_blocks = 0;
      }
   }


   switch (sol_)
   {
      case GMRES:
      {
         if ( myid_ == 0 && logging_ > 0 )
         {
            cout << "GMRES Solver Requested" << endl;
         }
         GMRESSolver gmres(HCurlFESpace_->GetComm());
         gmres.SetOperator(*A1.Ptr());
         gmres.SetRelTol(solOpts_.relTol);
         gmres.SetMaxIter(solOpts_.maxIter);
         gmres.SetKDim(solOpts_.kDim);
         gmres.SetPrintLevel(solOpts_.printLvl);

         gmres.Mult(RHS, E);
      }
      break;
      case FGMRES:
      {
         if ( myid_ == 0 && logging_ > 0 )
         {
            cout << "FGMRES Solver Requested" << endl;
         }
         /*
          HypreAMS amsr(dynamic_cast<HypreParMatrix&>(*PCOp.Ptr()),
                             HCurlFESpace_);
               ScaledOperator amsi(&amsr,
                                   (conv_ == ComplexOperator::HERMITIAN)?1.0:-1.0);

               BlockDiagonalPreconditioner BDP(blockTrueOffsets_);
               BDP.SetDiagonalBlock(0,&amsr);
               BDP.SetDiagonalBlock(1,&amsi);
               BDP.owns_blocks = 0;
         */
         FGMRESSolver fgmres(HCurlFESpace_->GetComm());
         if (BDP) { fgmres.SetPreconditioner(*BDP); }
         fgmres.SetOperator(*A1.Ptr());
         fgmres.SetRelTol(solOpts_.relTol);
         fgmres.SetMaxIter(solOpts_.maxIter);
         fgmres.SetKDim(solOpts_.kDim);
         fgmres.SetPrintLevel(solOpts_.printLvl);

         fgmres.Mult(RHS, E);

         // delete B1;
      }
      break;
      case MINRES:
      {
         if ( myid_ == 0 && logging_ > 0 )
         {
            cout << "MINRES Solver Requested" << endl;
         }
         MINRESSolver minres(HCurlFESpace_->GetComm());
         if (BDP) { minres.SetPreconditioner(*BDP); }
         minres.SetOperator(*A1.Ptr());
         minres.SetRelTol(solOpts_.relTol);
         minres.SetMaxIter(solOpts_.maxIter);
         minres.SetPrintLevel(solOpts_.printLvl);

         minres.Mult(RHS, E);
      }
      break;
#ifdef MFEM_USE_SUPERLU
      case SUPERLU:
      {
         if ( myid_ == 0 && logging_ > 0 )
         {
            cout << "SuperLU Solver Requested" << endl;
         }
         ComplexHypreParMatrix * A1Z = A1.As<ComplexHypreParMatrix>();
         HypreParMatrix * A1C = A1Z->GetSystemMatrix();
         SuperLURowLocMatrix A_SuperLU(*A1C);
         SuperLUSolver solver(MPI_COMM_WORLD);
         solver.SetOperator(A_SuperLU);
         solver.Mult(RHS, E);
         delete A1C;
         // delete A1Z;
      }
      break;
#endif
#ifdef MFEM_USE_STRUMPACK
      case STRUMPACK:
      {
         if ( myid_ == 0 && logging_ > 0 )
         {
            cout << "STRUMPACK Solver Requested" << endl;
         }
         //A1.SetOperatorOwner(false);
         ComplexHypreParMatrix * A1Z = A1.As<ComplexHypreParMatrix>();
         HypreParMatrix * A1C = A1Z->GetSystemMatrix();
         STRUMPACKRowLocMatrix A_STRUMPACK(*A1C);
         STRUMPACKSolver solver(0, NULL, MPI_COMM_WORLD);
         solver.SetPrintFactorStatistics(true);
         solver.SetPrintSolveStatistics(false);
         solver.SetKrylovSolver(strumpack::KrylovSolver::DIRECT);
         solver.SetReorderingStrategy(strumpack::ReorderingStrategy::METIS);
         solver.DisableMatching();
         solver.SetOperator(A_STRUMPACK);
         solver.SetFromCommandLine();
         solver.Mult(RHS, E);
         delete A1C;
         // delete A1Z;
      }
      break;
#endif
      default:
         MFEM_ABORT("Requested solver is not available.");
         break;
   };

   tic_toc.Stop();
   
   e_->Distribute(E);

   delete BDP;
   if (pci != pcr) { delete pci; }
   delete pcr;

   // delete A1;
   // delete A1C;
   /*
   // Initialize the magnetic vector potential with its boundary conditions
   *a_ = 0.0;
   // Apply surface currents if available
   if ( k_ )
   {
      SurfCur_->ComputeSurfaceCurrent(*k_);
      *a_ = *k_;
   }
   // Apply uniform B boundary condition on remaining surfaces
   a_->ProjectBdrCoefficientTangent(*aBCCoef_, non_k_bdr_);
   // Initialize the RHS vector to zero
   *jd_ = 0.0;
   // Initialize the volumetric current density
   if ( jr_ )
   {
      jr_->ProjectCoefficient(*jCoef_);
      // Compute the discretely divergence-free portion of jr_
      DivFreeProj_->Mult(*jr_, *j_);
      // Compute the dual of j_
      hCurlMass_->AddMult(*j_, *jd_);
   }
   // Initialize the Magnetization
   if ( m_ )
   {
      m_->ProjectCoefficient(*mCoef_);
      weakCurlMuInv_->AddMult(*m_, *jd_, mu0_);
   }
   // Apply Dirichlet BCs to matrix and right hand side and otherwise
   // prepare the linear system
   HypreParMatrix CurlMuInvCurl;
   HypreParVector A(HCurlFESpace_);
   HypreParVector RHS(HCurlFESpace_);
   curlMuInvCurl_->FormLinearSystem(ess_bdr_tdofs_, *a_, *jd_, CurlMuInvCurl,
                                    A, RHS);
   // Define and apply a parallel PCG solver for AX=B with the AMS
   // preconditioner from hypre.
   HypreAMS ams(CurlMuInvCurl, HCurlFESpace_);
   ams.SetSingularProblem();
   HyprePCG pcg (CurlMuInvCurl);
   pcg.SetTol(1e-12);
   pcg.SetMaxIter(50);
   pcg.SetPrintLevel(2);
   pcg.SetPreconditioner(ams);
   pcg.Mult(RHS, A);
   // Extract the parallel grid function corresponding to the finite
   // element approximation A. This is the local solution on each
   // processor.
   curlMuInvCurl_->RecoverFEMSolution(A, *jd_, *a_);
   // Compute the negative Gradient of the solution vector.  This is
   // the magnetic field corresponding to the scalar potential
   // represented by phi.
   curl_->Mult(*a_, *b_);
   // Compute magnetic field (H) from B and M
   if (myid_ == 0) { cout << "Computing H ... " << flush; }
   hDivHCurlMuInv_->Mult(*b_, *bd_);
   if ( m_ )
   {
      hDivHCurlMuInv_->AddMult(*m_, *bd_, -1.0 * mu0_);
   }
   HypreParMatrix MassHCurl;
   Vector BD, H;
   Array<int> dbc_dofs_h;
   hCurlMass_->FormLinearSystem(dbc_dofs_h, *h_, *bd_, MassHCurl, H, BD);
   HyprePCG pcgM(MassHCurl);
   pcgM.SetTol(1e-12);
   pcgM.SetMaxIter(500);
   pcgM.SetPrintLevel(0);
   HypreDiagScale diagM;
   pcgM.SetPreconditioner(diagM);
   pcgM.Mult(BD, H);
   hCurlMass_->RecoverFEMSolution(H, *bd_, *h_);
   */
   if ( myid_ == 0 && logging_ > 0 )
   {
     cout << " Solver done in " << tic_toc.RealTime() << " seconds." << endl;
   }
}

double
CPDSolver::GetError()
{
   double solErr = e_->ComputeL2Error(const_cast<VectorCoefficient&>(erCoef_),
                                      const_cast<VectorCoefficient&>(eiCoef_));

   return (solNorm_ > 0.0) ? solErr / solNorm_ : solErr;
}

void
CPDSolver::GetErrorEstimates(Vector & errors)
{
   if ( myid_ == 0 && logging_ > 0 )
   { cout << "Estimating Error ... " << flush; }

   // Space for the discontinuous (original) flux
   CurlCurlIntegrator flux_integrator(*muInvCoef_);
   RT_FECollection flux_fec(order_-1, pmesh_->SpaceDimension());
   ParFiniteElementSpace flux_fes(pmesh_, &flux_fec);

   // Space for the smoothed (conforming) flux
   double norm_p = 1;
   ND_FECollection smooth_flux_fec(order_, pmesh_->Dimension());
   ParFiniteElementSpace smooth_flux_fes(pmesh_, &smooth_flux_fec);

   L2ZZErrorEstimator(flux_integrator, e_->real(),
                      smooth_flux_fes, flux_fes, errors, norm_p);

   if ( myid_ == 0 && logging_ > 0 ) { cout << "done." << endl; }
}

void
CPDSolver::RegisterVisItFields(VisItDataCollection & visit_dc)
{
   visit_dc_ = &visit_dc;

   visit_dc.RegisterField("Re(E)", &e_->real());
   visit_dc.RegisterField("Im(E)", &e_->imag());
   // visit_dc.RegisterField("Er", e_r_);
   // visit_dc.RegisterField("Ei", e_i_);
   // visit_dc.RegisterField("B", b_);
   // visit_dc.RegisterField("H", h_);
   if ( j_ )
   {
      visit_dc.RegisterField("Re(J)", &j_->real());
      visit_dc.RegisterField("Im(J)", &j_->imag());
   }
   // if ( j_r_ ) { visit_dc.RegisterField("Jr", j_r_); }
   // if ( j_i_ ) { visit_dc.RegisterField("Ji", j_i_); }
   // if ( k_ ) { visit_dc.RegisterField("K", k_); }
   // if ( m_ ) { visit_dc.RegisterField("M", m_); }
   // if ( SurfCur_ ) { visit_dc.RegisterField("Psi", SurfCur_->GetPsi()); }
}

void
CPDSolver::WriteVisItFields(int it)
{
   if ( visit_dc_ )
   {
      if (myid_ == 0) { cout << "Writing VisIt files ..." << flush; }

      if ( j_ )
      {
         j_->ProjectCoefficient(*jrCoef_, *jiCoef_);
      }

      HYPRE_Int prob_size = this->GetProblemSize();
      visit_dc_->SetCycle(it);
      visit_dc_->SetTime(prob_size);
      visit_dc_->Save();

      if (myid_ == 0) { cout << " done." << endl; }
   }
}

void
CPDSolver::InitializeGLVis()
{
   if ( myid_ == 0 ) { cout << "Opening GLVis sockets." << endl; }

   socks_["Er"] = new socketstream;
   socks_["Er"]->precision(8);

   socks_["Ei"] = new socketstream;
   socks_["Ei"]->precision(8);

   // socks_["B"] = new socketstream;
   // socks_["B"]->precision(8);

   // socks_["H"] = new socketstream;
   // socks_["H"]->precision(8);

   if ( j_ )
   {
      socks_["Jr"] = new socketstream;
      socks_["Jr"]->precision(8);

      socks_["Ji"] = new socketstream;
      socks_["Ji"]->precision(8);
   }
   /*
   if ( k_ )
   {
      socks_["K"] = new socketstream;
      socks_["K"]->precision(8);
      socks_["Psi"] = new socketstream;
      socks_["Psi"]->precision(8);
   }
   if ( m_ )
   {
      socks_["M"] = new socketstream;
      socks_["M"]->precision(8);
   }
   */
   if ( myid_ == 0 ) { cout << "GLVis sockets open." << endl; }
}

void
CPDSolver::DisplayToGLVis()
{
   if (myid_ == 0) { cout << "Sending data to GLVis ..." << flush; }

   char vishost[] = "localhost";
   int  visport   = 19916;

   int Wx = 0, Wy = 0; // window position
   int Ww = 350, Wh = 350; // window size
   int offx = Ww+10, offy = Wh+45; // window offsets

   if (kCoef_)
   {
      VectorGridFunctionCoefficient e_r(&e_->real());
      VectorGridFunctionCoefficient e_i(&e_->imag());
      VectorSumCoefficient erCoef(e_r, e_i, *coskx_, *sinkx_);
      VectorSumCoefficient eiCoef(e_i, e_r, *coskx_, *negsinkx_);

      e_v_->ProjectCoefficient(erCoef, eiCoef);
   }
   else
   {
      e_v_ = e_;
   }

   VisualizeField(*socks_["Er"], vishost, visport,
                  e_v_->real(), "Electric Field, Re(E)", Wx, Wy, Ww, Wh);
   Wx += offx;

   VisualizeField(*socks_["Ei"], vishost, visport,
                  e_v_->imag(), "Electric Field, Im(E)", Wx, Wy, Ww, Wh);
   /*
   Wx += offx;
   VisualizeField(*socks_["B"], vishost, visport,
                  *b_, "Magnetic Flux Density (B)", Wx, Wy, Ww, Wh);
   Wx += offx;
   VisualizeField(*socks_["H"], vishost, visport,
                  *h_, "Magnetic Field (H)", Wx, Wy, Ww, Wh);
   Wx += offx;
   */
   if ( j_ )
   {
      Wx = 0; Wy += offy; // next line

      j_->ProjectCoefficient(*jrCoef_, *jiCoef_);

      if (kCoef_)
      {
         VectorGridFunctionCoefficient j_r(&j_->real());
         VectorGridFunctionCoefficient j_i(&j_->imag());
         VectorSumCoefficient jrCoef(j_r, j_i, *coskx_, *sinkx_);
         VectorSumCoefficient jiCoef(j_i, j_r, *coskx_, *negsinkx_);

         j_v_->ProjectCoefficient(jrCoef, jiCoef);
      }
      else
      {
         j_v_ = j_;
      }

      VisualizeField(*socks_["Jr"], vishost, visport,
                     j_v_->real(), "Current Density, Re(J)", Wx, Wy, Ww, Wh);
      Wx += offx;
      VisualizeField(*socks_["Ji"], vishost, visport,
                     j_v_->imag(), "Current Density, Im(J)", Wx, Wy, Ww, Wh);
   }
   Wx = 0; Wy += offy; // next line
   /*
   if ( k_ )
   {
      VisualizeField(*socks_["K"], vishost, visport,
                     *k_, "Surface Current Density (K)", Wx, Wy, Ww, Wh);
      Wx += offx;
      VisualizeField(*socks_["Psi"], vishost, visport,
                     *SurfCur_->GetPsi(),
                     "Surface Current Potential (Psi)", Wx, Wy, Ww, Wh);
      Wx += offx;
   }
   if ( m_ )
   {
      VisualizeField(*socks_["M"], vishost, visport,
                     *m_, "Magnetization (M)", Wx, Wy, Ww, Wh);
      Wx += offx;
   }
   */
   if (myid_ == 0) { cout << " done." << endl; }
}

void
CPDSolver::DisplayAnimationToGLVis()
{
   if (myid_ == 0) { cout << "Sending animation data to GLVis ..." << flush; }

   if (kCoef_)
   {
      VectorGridFunctionCoefficient e_r(&e_->real());
      VectorGridFunctionCoefficient e_i(&e_->imag());
      VectorSumCoefficient erCoef(e_r, e_i, *coskx_, *sinkx_);
      VectorSumCoefficient eiCoef(e_i, e_r, *coskx_, *negsinkx_);

      e_v_->ProjectCoefficient(erCoef, eiCoef);
   }
   else
   {
      e_v_ = e_;
   }

   Vector zeroVec(3); zeroVec = 0.0;
   VectorConstantCoefficient zeroCoef(zeroVec);

   double norm_r = e_v_->real().ComputeMaxError(zeroCoef);
   double norm_i = e_v_->imag().ComputeMaxError(zeroCoef);

   *e_t_ = e_v_->real();

   char vishost[] = "localhost";
   int  visport   = 19916;
   socketstream sol_sock(vishost, visport);
   sol_sock << "parallel " << num_procs_ << " " << myid_ << "\n";
   sol_sock.precision(8);
   sol_sock << "solution\n" << *pmesh_ << *e_t_
            << "window_title 'Harmonic Solution (t = 0.0 T)'"
            << "valuerange 0.0 " << max(norm_r, norm_i) << "\n"
            << "autoscale off\n"
            << "keys cvvv\n"
            << "pause\n" << flush;
   if (myid_ == 0)
      cout << "GLVis visualization paused."
           << " Press space (in the GLVis window) to resume it.\n";
   int num_frames = 24;
   int i = 0;
   while (sol_sock)
   {
      double t = (double)(i % num_frames) / num_frames;
      ostringstream oss;
      oss << "Harmonic Solution (t = " << t << " T)";

      add( cos( 2.0 * M_PI * t), e_v_->real(),
           -sin( 2.0 * M_PI * t), e_v_->imag(), *e_t_);
      sol_sock << "parallel " << num_procs_ << " " << myid_ << "\n";
      sol_sock << "solution\n" << *pmesh_ << *e_t_
               << "window_title '" << oss.str() << "'" << flush;
      i++;
   }
}

} // namespace plasma

} // namespace mfem

#endif // MFEM_USE_MPI
