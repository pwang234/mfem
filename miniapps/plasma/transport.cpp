//                         MFEM Example 18 - Parallel Version
//
// Compile with: make ex18
//
// Sample runs:
//
//        ex18p_split -rs 1 -nu 0.01 -ss 1 -dt 0.001 -c -1
//
// Description:  This example code solves the compressible Euler system of
//               equations, a model nonlinear hyperbolic PDE, with a
//               discontinuous Galerkin (DG) formulation.
//
//               Specifically, it solves for an exact solution of the equations
//               whereby a vortex is transported by a uniform flow. Since all
//               boundaries are periodic here, the method's accuracy can be
//               assessed by measuring the difference between the solution and
//               the initial condition at a later time when the vortex returns
//               to its initial location.
//
//               Note that as the order of the spatial discretization increases,
//               the timestep must become smaller. This example currently uses a
//               simple estimate derived by Cockburn and Shu for the 1D RKDG
//               method. An additional factor can be tuned by passing the --cfl
//               (or -c shorter) flag.
//
//               The example demonstrates user-defined bilinear and nonlinear
//               form integrators for systems of equations that are defined with
//               block vectors, and how these are used with an operator for
//               explicit time integrators. In this case the system also
//               involves an external approximate Riemann solver for the DG
//               interface flux. It also demonstrates how to use GLVis for
//               in-situ visualization of vector grid functions.
//
//               We recommend viewing examples 9, 14 and 17 before viewing this
//               example.

#include "mfem.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

#include "../common/pfem_extras.hpp"
#include "transport_solver.hpp"

using namespace std;
using namespace mfem;
using namespace mfem::miniapps;
using namespace mfem::plasma;

// Choice for the problem setup. See InitialCondition in ex18.hpp.
int problem_;

// Equation constant parameters.
int num_species_ = -1;
int num_equations_ = -1;
const double specific_heat_ratio_ = 1.4;
const double gas_constant_ = 1.0;

// Scalar coefficient for diffusion of momentum
static double diffusion_constant_ = 0.1;
static double dg_sigma_ = -1.0;
static double dg_kappa_ = -1.0;

static double B_max_ = 1.0;
static double v_max_ = 0.0;

// Maximum characteristic speed (updated by integrators)
static double max_char_speed_;

// Background fields and initial conditions
static int prob_ = 4;
static int gamma_ = 10;
static double alpha_ = NAN;
static double chi_max_ratio_ = 1.0;
static double chi_min_ratio_ = 1.0;

void ChiFunc(const Vector &x, DenseMatrix &M)
{
   M.SetSize(2);

   switch (prob_)
   {
      case 1:
      {
         double cx = cos(M_PI * x[0]);
         double cy = cos(M_PI * x[1]);
         double sx = sin(M_PI * x[0]);
         double sy = sin(M_PI * x[1]);

         double den = cx * cx * sy * sy + sx * sx * cy * cy;

         M(0,0) = chi_max_ratio_ * sx * sx * cy * cy + sy * sy * cx * cx;
         M(1,1) = chi_max_ratio_ * sy * sy * cx * cx + sx * sx * cy * cy;

         M(0,1) = (1.0 - chi_max_ratio_) * cx * cy * sx * sy;
         M(1,0) = M(0,1);

         M *= 1.0 / den;
      }
      break;
      case 2:
      case 4:
      {
         double a = 0.4;
         double b = 0.8;

         double den = pow(b * b * x[0], 2) + pow(a * a * x[1], 2);

         M(0,0) = chi_max_ratio_ * pow(a * a * x[1], 2) + pow(b * b * x[0], 2);
         M(1,1) = chi_max_ratio_ * pow(b * b * x[0], 2) + pow(a * a * x[1], 2);

         M(0,1) = (1.0 - chi_max_ratio_) * pow(a * b, 2) * x[0] * x[1];
         M(1,0) = M(0,1);

         M *= 1.0 / den;
      }
      break;
      case 3:
      {
         double ca = cos(alpha_);
         double sa = sin(alpha_);

         M(0,0) = 1.0 + (chi_max_ratio_ - 1.0) * ca * ca;
         M(1,1) = 1.0 + (chi_max_ratio_ - 1.0) * sa * sa;

         M(0,1) = (chi_max_ratio_ - 1.0) * ca * sa;
         M(1,0) = (chi_max_ratio_ - 1.0) * ca * sa;
      }
      break;
   }
}

double TFunc(const Vector &x, double t)
{
   switch (prob_)
   {
      case 1:
      {
         double e = exp(-2.0 * M_PI * M_PI * t);
         return sin(M_PI * x[0]) * sin(M_PI * x[1]) * (1.0 - e);
      }
      case 2:
      {
         double a = 0.4;
         double b = 0.8;

         double r = pow(x[0] / a, 2) + pow(x[1] / b, 2);
         double e = exp(-0.25 * t * M_PI * M_PI / (a * b) );

         return cos(0.5 * M_PI * sqrt(r)) * (1.0 - e);
      }
      case 3:
         return pow(sin(M_PI * x[0]) * sin(M_PI * x[1]), gamma_);
      case 4:
      {
         double a = 0.4;
         double b = 0.8;

         double r = pow(x[0] / a, 2) + pow(x[1] / b, 2);
         double rs = pow(x[0] - 0.5 * a, 2) + pow(x[1] - 0.5 * b, 2);
         return cos(0.5 * M_PI * sqrt(r)) + 0.5 * exp(-400.0 * rs);
      }
   }
   return 0.0;
}

void bFunc(const Vector &x, Vector &B)
{
   B.SetSize(2);

   switch (prob_)
   {
      case 1:
      {
         double cx = cos(M_PI * x[0]);
         double cy = cos(M_PI * x[1]);
         double sx = sin(M_PI * x[0]);
         double sy = sin(M_PI * x[1]);

         double den = cx * cx * sy * sy + sx * sx * cy * cy;

         B[0] =  sx * cy;
         B[1] = -sy * cx;
         B *= 1.0 / sqrt(den);
      }
      break;
      case 2:
      case 4:
      {
         double a = 0.4;
         double b = 0.8;

         // double den = pow(b * b * x[0], 2) + pow(a * a * x[1], 2);

         B[0] =  a * x[1] / (b * b);
         B[1] = -x[0] / a;
         // B *= 1.0 / sqrt(den);
         B *= B_max_;
      }
      break;
      case 3:
      {
         double ca = cos(alpha_);
         double sa = sin(alpha_);

         B[0] = ca;
         B[1] = sa;
      }
      break;
   }
}

void bbTFunc(const Vector &x, DenseMatrix &M)
{
   M.SetSize(2);

   switch (prob_)
   {
      case 1:
      {
         double cx = cos(M_PI * x[0]);
         double cy = cos(M_PI * x[1]);
         double sx = sin(M_PI * x[0]);
         double sy = sin(M_PI * x[1]);

         double den = cx * cx * sy * sy + sx * sx * cy * cy;

         M(0,0) = sx * sx * cy * cy;
         M(1,1) = sy * sy * cx * cx;

         M(0,1) =  -1.0 * cx * cy * sx * sy;
         M(1,0) = M(0,1);

         M *= 1.0 / den;
      }
      break;
      case 2:
      case 4:
      {
         double a = 0.4;
         double b = 0.8;

         double den = pow(b * b * x[0], 2) + pow(a * a * x[1], 2);

         M(0,0) = pow(a * a * x[1], 2);
         M(1,1) = pow(b * b * x[0], 2);

         M(0,1) = -1.0 * pow(a * b, 2) * x[0] * x[1];
         M(1,0) = M(0,1);

         M *= 1.0 / den;
      }
      break;
      case 3:
      {
         double ca = cos(alpha_);
         double sa = sin(alpha_);

         M(0,0) = ca * ca;
         M(1,1) = sa * sa;

         M(0,1) = ca * sa;
         M(1,0) = ca * sa;
      }
      break;
   }
}

// Initial condition
void InitialCondition(const Vector &x, Vector &y);

int main(int argc, char *argv[])
{
   // 1. Initialize MPI.
   MPI_Session mpi(argc, argv);

   // 2. Parse command-line options.
   problem_ = 1;
   const char *mesh_file = "ellipse_origin_h0pt0625_o3.mesh";
   int ser_ref_levels = 0;
   int par_ref_levels = 1;
   int order = 3;
   int ode_split_solver_type = 1;
   int ode_exp_solver_type = -1;
   int ode_imp_solver_type = -1;
   double t_final = -1.0;
   double dt = -0.01;
   double dt_rel_tol = 0.1;
   double cfl = 0.3;
   bool visualization = true;
   int vis_steps = 50;

   Array<int> ion_charges;
   Vector ion_masses;

   int precision = 8;
   cout.precision(precision);

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&problem_, "-p", "--problem",
                  "Problem setup to use. See options in velocity_function().");
   args.AddOption(&ser_ref_levels, "-rs", "--refine-serial",
                  "Number of times to refine the mesh uniformly before parallel"
                  " partitioning, -1 for auto.");
   args.AddOption(&par_ref_levels, "-rp", "--refine-parallel",
                  "Number of times to refine the mesh uniformly after parallel"
                  " partitioning.");
   args.AddOption(&order, "-o", "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&ode_split_solver_type, "-ss", "--ode-split-solver",
                  "ODE Split solver:\n"
                  "            1 - First Order Fractional Step,\n"
                  "            2 - Strang Splitting (2nd Order).");
   args.AddOption(&ode_exp_solver_type, "-se", "--ode-exp-solver",
                  "ODE Explicit solver:\n"
                  "            1 - Forward Euler,\n\t"
                  "            2 - RK2 SSP, 3 - RK3 SSP, 4 - RK4, 6 - RK6.");
   args.AddOption(&ode_imp_solver_type, "-si", "--ode-imp-solver",
                  "ODE Implicit solver: L-stable methods\n\t"
                  "            1 - Backward Euler,\n\t"
                  "            2 - SDIRK23, 3 - SDIRK33,\n\t"
                  "            A-stable methods (not L-stable)\n\t"
                  "            22 - ImplicitMidPointSolver,\n\t"
                  "            23 - SDIRK23, 34 - SDIRK34.");
   args.AddOption(&t_final, "-tf", "--t-final",
                  "Final time; start time is 0.");
   args.AddOption(&dt, "-dt", "--time-step",
                  "Time step. Positive number skips CFL timestep calculation.");
   args.AddOption(&dt_rel_tol, "-dttol", "--time-step-tolerance",
                  "Time step will only be adjusted if the relative difference "
                  "exceeds dttol.");
   args.AddOption(&cfl, "-c", "--cfl-number",
                  "CFL number for timestep calculation.");
   args.AddOption(&ion_charges, "-qi", "--ion-charges",
                  "Charges of the various species "
                  "(in units of electron charge)");
   args.AddOption(&ion_masses, "-mi", "--ion-masses",
                  "Masses of the various species (in amu)");
   args.AddOption(&diffusion_constant_, "-nu", "--diffusion-constant",
                  "Diffusion constant used in momentum equation.");
   args.AddOption(&dg_sigma_, "-dgs", "--sigma",
                  "One of the two DG penalty parameters, typically +1/-1."
                  " See the documentation of class DGDiffusionIntegrator.");
   args.AddOption(&dg_kappa_, "-dgk", "--kappa",
                  "One of the two DG penalty parameters, should be positive."
                  " Negative values are replaced with (order+1)^2.");
   args.AddOption(&B_max_, "-B", "--B-magnitude",
                  "");
   args.AddOption(&v_max_, "-v", "--velocity",
                  "");
   args.AddOption(&chi_max_ratio_, "-chi-max", "--chi-max-ratio",
                  "Ratio of chi_max_parallel/chi_perp.");
   args.AddOption(&chi_min_ratio_, "-chi-min", "--chi-min-ratio",
                  "Ratio of chi_min_parallel/chi_perp.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&vis_steps, "-vs", "--visualization-steps",
                  "Visualize every n-th timestep.");

   args.Parse();
   if (!args.Good())
   {
      if (mpi.Root()) { args.PrintUsage(cout); }
      return 1;
   }
   if (ode_exp_solver_type < 0)
   {
      ode_exp_solver_type = ode_split_solver_type;
   }
   if (ode_imp_solver_type < 0)
   {
      ode_imp_solver_type = ode_split_solver_type;
   }
   if (ion_charges.Size() == 0)
   {
      ion_charges.SetSize(1);
      ion_charges[0] =  1.0;
   }
   if (ion_masses.Size() == 0)
   {
      ion_masses.SetSize(1);
      ion_masses[0] = 2.01410178;
   }
   if (dg_kappa_ < 0)
   {
      dg_kappa_ = (order+1)*(order+1);
   }
   if (t_final < 0.0)
   {
      if (strcmp(mesh_file, "../data/periodic-hexagon.mesh") == 0)
      {
         t_final = 3.0;
      }
      else if (strcmp(mesh_file, "../data/periodic-square.mesh") == 0)
      {
         t_final = 2.0;
      }
      else
      {
         t_final = 1.0;
      }
   }
   if (mpi.Root()) { args.PrintOptions(cout); }

   // 3. Read the mesh from the given mesh file. This example requires a 2D
   //    periodic mesh, such as ../data/periodic-square.mesh.
   Mesh mesh(mesh_file, 1, 1);
   const int dim = mesh.Dimension();

   MFEM_ASSERT(dim == 2, "Need a two-dimensional mesh for the problem definition");

   num_species_   = ion_charges.Size();
   num_equations_ = (num_species_ + 1) * (dim + 2);

   // 4. Define the ODE solver used for time integration. Several explicit
   //    Runge-Kutta methods are available.
   ODESolver *ode_exp_solver = NULL;
   ODESolver *ode_imp_solver = NULL;
   switch (ode_exp_solver_type)
   {
      case 1: ode_exp_solver = new ForwardEulerSolver; break;
      case 2: ode_exp_solver = new RK2Solver(1.0); break;
      case 3: ode_exp_solver = new RK3SSPSolver; break;
      case 4: ode_exp_solver = new RK4Solver; break;
      case 6: ode_exp_solver = new RK6Solver; break;
      default:
         if (mpi.Root())
         {
            cout << "Unknown Explicit ODE solver type: "
                 << ode_exp_solver_type << '\n';
         }
         return 3;
   }
   switch (ode_imp_solver_type)
   {
      // Implicit L-stable methods
      case 1:  ode_imp_solver = new BackwardEulerSolver; break;
      case 2:  ode_imp_solver = new SDIRK23Solver(2); break;
      case 3:  ode_imp_solver = new SDIRK33Solver; break;
      // Implicit A-stable methods (not L-stable)
      case 22: ode_imp_solver = new ImplicitMidpointSolver; break;
      case 23: ode_imp_solver = new SDIRK23Solver; break;
      case 34: ode_imp_solver = new SDIRK34Solver; break;
      default:
         if (mpi.Root())
         {
            cout << "Unknown Implicit ODE solver type: "
                 << ode_imp_solver_type << '\n';
         }
         return 3;
   }

   // 5. Refine the mesh in serial to increase the resolution. In this example
   //    we do 'ser_ref_levels' of uniform refinement, where 'ser_ref_levels' is
   //    a command-line parameter.
   for (int lev = 0; lev < ser_ref_levels; lev++)
   {
      mesh.UniformRefinement();
   }

   // 6. Define a parallel mesh by a partitioning of the serial mesh. Refine
   //    this mesh further in parallel to increase the resolution. Once the
   //    parallel mesh is defined, the serial mesh can be deleted.
   ParMesh pmesh(MPI_COMM_WORLD, mesh);
   mesh.Clear();
   for (int lev = 0; lev < par_ref_levels; lev++)
   {
      pmesh.UniformRefinement();
   }

   // 7. Define the discontinuous DG finite element space of the given
   //    polynomial order on the refined mesh.
   DG_FECollection fec(order, dim);
   // Finite element space for a scalar (thermodynamic quantity)
   ParFiniteElementSpace sfes(&pmesh, &fec);
   // Finite element space for a mesh-dim vector quantity (momentum)
   ParFiniteElementSpace vfes(&pmesh, &fec, dim, Ordering::byNODES);
   // Finite element space for all variables together (full thermodynamic state)
   ParFiniteElementSpace ffes(&pmesh, &fec, num_equations_, Ordering::byNODES);

   RT_FECollection fec_rt(order, dim);
   // Finite element space for the magnetic field
   ParFiniteElementSpace fes_rt(&pmesh, &fec_rt);

   // This example depends on this ordering of the space.
   MFEM_ASSERT(ffes.GetOrdering() == Ordering::byNODES, "");

   HYPRE_Int glob_size_sca = sfes.GlobalTrueVSize();
   HYPRE_Int glob_size_tot = ffes.GlobalTrueVSize();
   HYPRE_Int glob_size_rt  = fes_rt.GlobalTrueVSize();
   if (mpi.Root())
   { cout << "Number of unknowns per field: " << glob_size_sca << endl; }
   if (mpi.Root())
   { cout << "Total number of unknowns:     " << glob_size_tot << endl; }
   if (mpi.Root())
   { cout << "Number of magnetic field unknowns: " << glob_size_rt << endl; }

   //ConstantCoefficient nuCoef(diffusion_constant_);
   // MatrixFunctionCoefficient nuCoef(dim, ChiFunc);

   // 8. Define the initial conditions, save the corresponding mesh and grid
   //    functions to a file. This can be opened with GLVis with the -gc option.

   // The solution u has components {particle density, x-velocity,
   // y-velocity, temperature} for each species (species loop is the outermost).
   // These are stored contiguously in the BlockVector u_block.
   Array<int> offsets(num_equations_ + 1);
   for (int k = 0; k <= num_equations_; k++)
   {
      offsets[k] = k * sfes.GetNDofs();
   }
   BlockVector u_block(offsets);

   Array<int> n_offsets(num_species_ + 2);
   for (int k = 0; k <= num_species_ + 1; k++)
   {
      n_offsets[k] = offsets[k];
   }
   BlockVector n_block(u_block, n_offsets);

   // Momentum and Energy grid functions on for visualization.
   /*
   ParGridFunction density(&fes, u_block.GetData());
   ParGridFunction velocity(&dfes, u_block.GetData() + offsets[1]);
   ParGridFunction temperature(&fes, u_block.GetData() + offsets[dim+1]);
   */

   // Initialize the state.
   VectorFunctionCoefficient u0(num_equations_, InitialCondition);
   ParGridFunction sol(&ffes, u_block.GetData());
   sol.ProjectCoefficient(u0);

   VectorFunctionCoefficient BCoef(dim, bFunc);
   ParGridFunction B(&fes_rt);
   B.ProjectCoefficient(BCoef);

   // Output the initial solution.
   /*
   {
      ostringstream mesh_name;
      mesh_name << "transport-mesh." << setfill('0') << setw(6)
      << mpi.WorldRank();
      ofstream mesh_ofs(mesh_name.str().c_str());
      mesh_ofs.precision(precision);
      mesh_ofs << pmesh;

      for (int i = 0; i < num_species_; i++)
   for (int j = 0; j < dim + 2; j++)
   {
      int k = 0;
      ParGridFunction uk(&sfes, u_block.GetBlock(k));
      ostringstream sol_name;
      sol_name << "species-" << i << "-field-" << j << "-init."
          << setfill('0') << setw(6) << mpi.WorldRank();
      ofstream sol_ofs(sol_name.str().c_str());
      sol_ofs.precision(precision);
      sol_ofs << uk;
   }
   }
   */

   // 9. Set up the nonlinear form corresponding to the DG discretization of the
   //    flux divergence, and assemble the corresponding mass matrix.
   /*
   MixedBilinearForm Aflux(&dfes, &fes);
   Aflux.AddDomainIntegrator(new DomainIntegrator(dim, num_equations_));
   Aflux.Assemble();

   ParNonlinearForm A(&vfes);
   RiemannSolver rsolver(num_equations_, specific_heat_ratio_);
   A.AddInteriorFaceIntegrator(new FaceIntegrator(rsolver, dim,
                    num_equations_));

   // 10. Define the time-dependent evolution operator describing the ODE
   //     right-hand side, and perform time-integration (looping over the time
   //     iterations, ti, with a time-step dt).
   AdvectionTDO adv(vfes, A, Aflux.SpMat(), num_equations_,
                    specific_heat_ratio_);
   DiffusionTDO diff(fes, dfes, vfes, nuCoef, dg_sigma_, dg_kappa_);
   */
   TransportSolver transp(ode_imp_solver, ode_exp_solver, sfes, vfes, ffes,
                          n_block, B, ion_charges, ion_masses);

   // Visualize the density, momentum, and energy
   vector<socketstream> dout(num_species_+1), vout(num_species_+1),
          tout(num_species_+1), xout(num_species_+1), eout(num_species_+1);

   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;

      int Wx = 0, Wy = 0; // window position
      int Ww = 275, Wh = 250; // window size
      int offx = Ww + 3, offy = Wh + 25; // window offsets

      MPI_Barrier(pmesh.GetComm());

      for (int i=0; i<=num_species_; i++)
      {
         int doff = offsets[i];
         int voff = offsets[i * dim + num_species_ + 1];
         int toff = offsets[i + (num_species_ + 1) * (dim + 1)];
         double * u_data = u_block.GetData();
         ParGridFunction density(&sfes, u_data + doff);
         ParGridFunction velocity(&vfes, u_data + voff);
         ParGridFunction temperature(&sfes, u_data + toff);


         ParGridFunction chi_para(&sfes);
         ParGridFunction eta_para(&sfes);
         if (i==0)
         {
            ChiParaCoefficient chiParaCoef(n_block, ion_charges);
            chiParaCoef.SetT(temperature);
            chi_para.ProjectCoefficient(chiParaCoef);

            EtaParaCoefficient etaParaCoef(n_block, ion_charges);
            etaParaCoef.SetT(temperature);
            eta_para.ProjectCoefficient(etaParaCoef);
         }
         else
         {
            ChiParaCoefficient chiParaCoef(n_block, i - 1,
                                           ion_charges,
                                           ion_masses);
            chiParaCoef.SetT(temperature);
            chi_para.ProjectCoefficient(chiParaCoef);

            EtaParaCoefficient etaParaCoef(n_block, i - 1,
                                           ion_charges,
                                           ion_masses);
            etaParaCoef.SetT(temperature);
            eta_para.ProjectCoefficient(etaParaCoef);
         }

         ostringstream head;
         if (i==0)
         {
            head << "Electron";
         }
         else
         {
            head << "Species " << i;
         }

         ostringstream doss;
         doss << head.str() << " Density";
         VisualizeField(dout[i], vishost, visport,
                        density, doss.str().c_str(),
                        Wx, Wy, Ww, Wh);
         Wx += offx;

         ostringstream voss; voss << head.str() << " Velocity";
         VisualizeField(vout[i], vishost, visport,
                        velocity, voss.str().c_str(),
                        Wx, Wy, Ww, Wh, NULL, true);
         Wx += offx;

         ostringstream toss; toss << head.str() << " Temperature";
         VisualizeField(tout[i], vishost, visport,
                        temperature, toss.str().c_str(),
                        Wx, Wy, Ww, Wh);

         Wx += offx;

         ostringstream xoss; xoss << head.str() << " Chi Parallel";
         VisualizeField(xout[i], vishost, visport,
                        chi_para, xoss.str().c_str(),
                        Wx, Wy, Ww, Wh);

         Wx += offx;

         ostringstream eoss; eoss << head.str() << " Eta Parallel";
         VisualizeField(eout[i], vishost, visport,
                        eta_para, eoss.str().c_str(),
                        Wx, Wy, Ww, Wh);

         Wx -= 4 * offx;
         Wy += offy;
      }
   }
   exit(0);

   // Determine the minimum element size.
   double hmin;
   if (cfl > 0)
   {
      double my_hmin = pmesh.GetElementSize(0, 1);
      for (int i = 1; i < pmesh.GetNE(); i++)
      {
         my_hmin = min(pmesh.GetElementSize(i, 1), my_hmin);
      }
      // Reduce to find the global minimum element size
      MPI_Allreduce(&my_hmin, &hmin, 1, MPI_DOUBLE, MPI_MIN, pmesh.GetComm());
   }

   // Start the timer.
   tic_toc.Clear();
   tic_toc.Start();
   /*
   double t = 0.0;
   adv.SetTime(t);
   ode_exp_solver->Init(adv);

   diff.SetTime(t);
   ode_imp_solver->Init(diff);

   if (cfl > 0)
   {
      // Find a safe dt, using a temporary vector. Calling Mult() computes the
      // maximum char speed at all quadrature points on all faces.
      max_char_speed_ = 0.;
      Vector z(sol.Size());
      A.Mult(sol, z);
      // Reduce to find the global maximum wave speed
      {
         double all_max_char_speed;
         MPI_Allreduce(&max_char_speed_, &all_max_char_speed,
                       1, MPI_DOUBLE, MPI_MAX, pmesh.GetComm());
         max_char_speed_ = all_max_char_speed;
      }
      dt = cfl * hmin / max_char_speed_ / (2*order+1);

      if (mpi.Root())
      {
         cout << "Minimum Edge Length: " << hmin << '\n';
         cout << "Maximum Speed:       " << max_char_speed_ << '\n';
         cout << "CFL fraction:        " << cfl << '\n';
         cout << "Initial Time Step:   " << dt << '\n';
      }
   }

   // Integrate in time.
   bool done = false;
   for (int ti = 0; !done; )
   {
      double dt_real = min(dt, t_final - t);

      if (ode_split_solver_type == 1)
      {
         double dt_imp = dt_real;
         double dt_exp = dt_real;
         double t_imp = t;
         ode_imp_solver->Step(sol, t_imp, dt_imp);
         ode_exp_solver->Step(sol, t, dt_exp);
      }
      else
      {
         double dt_imp = 0.5 * dt_real;
         double t_imp = t;
         double dt_exp = dt_real;
         ode_imp_solver->Step(sol, t_imp, dt_imp);
         ode_exp_solver->Step(sol, t, dt_exp);
         ode_imp_solver->Step(sol, t_imp, dt_imp);
      }

      if (cfl > 0)
      {
         // Reduce to find the global maximum wave speed
         {
            double all_max_char_speed;
            MPI_Allreduce(&max_char_speed_, &all_max_char_speed,
                          1, MPI_DOUBLE, MPI_MAX, pmesh.GetComm());
            max_char_speed_ = all_max_char_speed;
         }
         double new_dt = cfl * hmin / max_char_speed_ / (2*order+1);

         if (fabs(dt - new_dt) > dt_rel_tol * dt)
         {
            dt = new_dt;
            if (mpi.Root())
            {
               cout << "Adjusting Time Step\n";
               cout << "Minimum Edge Length: " << hmin << '\n';
               cout << "Maximum Speed:       " << max_char_speed_ << '\n';
               cout << "CFL fraction:        " << cfl << '\n';
               cout << "New Time Step:       " << new_dt << '\n';
            }
         }
      }
      ti++;

      done = (t >= t_final - 1e-8*dt);
      if (done || ti % vis_steps == 0)
      {
         if (mpi.Root())
         {
            cout << "time step: " << ti << ", time: " << t << endl;
         }
         if (visualization)
         {
            MPI_Barrier(pmesh.GetComm());
            for (int i=0; i<num_species_; i++)
            {
       int doff = offsets[i * (dim + 2) + 0];
       int voff = offsets[i * (dim + 2) + 1];
       int toff = offsets[i * (dim + 2) + dim + 1];
          double * u_data = u_block.GetData();
          ParGridFunction density(&fes, u_data + doff);
          ParGridFunction velocity(&dfes, u_data + voff);
          ParGridFunction temperature(&fes, u_data + toff);
               dout[i] << "parallel " << mpi.WorldSize()
             << " " << mpi.WorldRank() << "\n";
          dout[i] << "solution\n" << pmesh << density << flush;

          vout[i] << "parallel " << mpi.WorldSize()
             << " " << mpi.WorldRank() << "\n";
          vout[i] << "solution\n" << pmesh << velocity << flush;

          tout[i] << "parallel " << mpi.WorldSize()
             << " " << mpi.WorldRank() << "\n";
          tout[i] << "solution\n" << pmesh << temperature << flush;
       }
    }
      }
   }
   */
   tic_toc.Stop();
   if (mpi.Root()) { cout << " done, " << tic_toc.RealTime() << "s." << endl; }

   // 11. Save the final solution. This output can be viewed later using GLVis:
   //     "glvis -np 4 -m transport-mesh -g species-0-field-0-final".
   {
      int k = 0;
      for (int i = 0; i < num_species_; i++)
         for (int j = 0; j < dim + 2; j++)
         {
            ParGridFunction uk(&sfes, u_block.GetBlock(k));
            ostringstream sol_name;
            sol_name << "species-" << i << "-field-" << j << "-final."
                     << setfill('0') << setw(6) << mpi.WorldRank();
            ofstream sol_ofs(sol_name.str().c_str());
            sol_ofs.precision(precision);
            sol_ofs << uk;
            k++;
         }
   }

   // 12. Compute the L2 solution error summed for all components.
   if ((t_final == 2.0 &&
        strcmp(mesh_file, "../data/periodic-square.mesh") == 0) ||
       (t_final == 3.0 &&
        strcmp(mesh_file, "../data/periodic-hexagon.mesh") == 0))
   {
      const double error = sol.ComputeLpError(2, u0);
      if (mpi.Root()) { cout << "Solution error: " << error << endl; }
   }

   // Free the used memory.
   delete ode_exp_solver;
   delete ode_imp_solver;

   return 0;
}

// Initial condition
void InitialCondition(const Vector &x, Vector &y)
{
   MFEM_ASSERT(x.Size() == 2, "");
   /*
   double radius = 0, Minf = 0, beta = 0;
   if (problem_ == 1)
   {
      // "Fast vortex"
      radius = 0.2;
      Minf = 0.5;
      beta = 1. / 5.;
   }
   else if (problem_ == 2)
   {
      // "Slow vortex"
      radius = 0.2;
      Minf = 0.05;
      beta = 1. / 50.;
   }
   else
   {
      mfem_error("Cannot recognize problem."
                 "Options are: 1 - fast vortex, 2 - slow vortex");
   }

   const double xc = 0.0, yc = 0.0;

   // Nice units
   const double vel_inf = 1.;
   const double den_inf = 1.;

   // Derive remainder of background state from this and Minf
   const double pres_inf = (den_inf / specific_heat_ratio_) * (vel_inf / Minf) *
                           (vel_inf / Minf);
   const double temp_inf = pres_inf / (den_inf * gas_constant_);

   double r2rad = 0.0;
   r2rad += (x(0) - xc) * (x(0) - xc);
   r2rad += (x(1) - yc) * (x(1) - yc);
   r2rad /= (radius * radius);

   const double shrinv1 = 1.0 / (specific_heat_ratio_ - 1.);

   const double velX = vel_inf * (1 - beta * (x(1) - yc) / radius * exp(
                                     -0.5 * r2rad));
   const double velY = vel_inf * beta * (x(0) - xc) / radius * exp(-0.5 * r2rad);
   const double vel2 = velX * velX + velY * velY;

   const double specific_heat = gas_constant_ * specific_heat_ratio_ * shrinv1;
   const double temp = temp_inf - 0.5 * (vel_inf * beta) *
                       (vel_inf * beta) / specific_heat * exp(-r2rad);

   const double den = den_inf * pow(temp/temp_inf, shrinv1);
   const double pres = den * gas_constant_ * temp;
   const double energy = shrinv1 * pres / den + 0.5 * vel2;

   y(0) = den;
   y(1) = den * velX;
   y(2) = den * velY;
   y(3) = den * energy;
   */
   // double VMag = 1e2;
   if (y.Size() != num_equations_) { cout << "y is wrong size!" << endl; }

   int dim = 2;
   double a = 0.4;
   double b = 0.8;

   Vector V(2);
   bFunc(x, V);
   V *= (v_max_ / B_max_) * sqrt(pow(x[0]/a,2)+pow(x[1]/b,2));

   double den = 1.0e18;
   for (int i=1; i<=num_species_; i++)
   {
      y(i) = den;
      y(i * dim + num_species_ + 1) = V(0);
      y(i * dim + num_species_ + 2) = V(1);
      y(i + (num_species_ + 1) * (dim + 1)) = 10.0 * TFunc(x, 0.0);
   }

   // Impose neutrality
   y(0) = 0.0;
   for (int i=1; i<=num_species_; i++)
   {
      y(0) += y(i);
   }
   y(num_species_ + 1) = V(0);
   y(num_species_ + 2) = V(1);
   y((num_species_ + 1) * (dim + 1)) = 5.0 * TFunc(x, 0.0);

   // y.Print(cout, dim+2); cout << endl;
}
