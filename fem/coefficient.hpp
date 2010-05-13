// Copyright (c) 2010,  Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
// This file is part of the MFEM library.  See file COPYRIGHT for details.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#ifndef MFEM_COEFFICIENT
#define MFEM_COEFFICIENT

/// Base class Coefficient
class Coefficient
{
public:

   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip) = 0;
   virtual void Read(istream &in) = 0;

   virtual ~Coefficient() { };
};


/// Subclass constant coefficient.
class ConstantCoefficient: public Coefficient
{
public:

   double constant;

   /// c is value of constant function
   ConstantCoefficient(double c = 1.0) { constant=c; };

   /// Evaluate the coefficient
   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip)
   { return(constant); };

   virtual void Read(istream &in) { in >> constant; };
};

/// class for piecewise constant coefficient
class PWConstCoefficient : public Coefficient
{
private:
   Vector constants;

public:

   /// Constructs a piecewise constant coefficient in NumOfSubD subdomains
   PWConstCoefficient(int NumOfSubD = 0) : constants(NumOfSubD)
   { constants = 0.0; };

   /** c should be a vector defined by attributes, so for region with
       attribute i  c[i] is the coefficient in that region */
   PWConstCoefficient(Vector &c)
   { constants.SetSize(c.Size()); constants=c; };

   /// Member function to access or modify the value of the i-th constant
   double &operator()(int i) { return constants(i-1); };

   /// Set domain constants equal to the same constant c
   void operator=(double c) { constants = c; };

   /// Returns the number of constants
   int GetNConst() { return constants.Size(); };

   /// Evaluate the coefficient function
   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip);

   virtual void Read(istream &in);
};

/// class for C-function coefficient
class FunctionCoefficient:public Coefficient
{
private:
   double (*Function)(Vector &);
   Vector transip;

public:

   /// f should be a pointer to a c function
   FunctionCoefficient( double (*f)(Vector &) ) { Function=f; };

   /// Evaluate coefficient
   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip);

   virtual void Read(istream &in) { };
};

class GridFunction;

/** Coefficient defined by a GridFunction;
    This coefficient is mesh dependent.     */
class GridFunctionCoefficient : public Coefficient
{
private:
   GridFunction *GridF;
   int Component;

public:
   /** Construct GridFunctionCoefficient from a given GridFunction
       and optionally which component to use if it is a vector
       gridfunction. */
   GridFunctionCoefficient (GridFunction *gf, int comp = 1)
   { GridF = gf; Component = comp; }

   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip);

   virtual void Read(istream &in)
   { mfem_error ("GridFunctionCoefficient::Read()"); }
};

class TransformedCoefficient : public Coefficient
{
private:
   Coefficient * Q1;
   Coefficient * Q2;
   double (*Transform1)(double);
   double (*Transform2)(double,double);

public:
   TransformedCoefficient (Coefficient * q,double (*F)(double))
      : Q1(q), Transform1(F) { Q2 = 0; Transform2 = 0; }
   TransformedCoefficient (Coefficient * q1,Coefficient * q2,
                           double (*F)(double,double))
      : Q1(q1), Q2(q2), Transform2(F) { Transform1 = 0; }

   virtual double Eval(ElementTransformation &T, const IntegrationPoint &ip)
   { if (Q2) return (*Transform2)(Q1 -> Eval(T,ip),Q2 -> Eval(T,ip));
   else return (*Transform1)(Q1 -> Eval(T,ip)); }

   virtual void Read(istream &in) { };
};

/// Delta function coefficient
class DeltaCoefficient : public Coefficient
{
private:
   double center[3], scale;

public:
   DeltaCoefficient();
   DeltaCoefficient(double x, double y, double s)
   { center[0] = x; center[1] = y; center[2] = 0.; scale = s; }
   DeltaCoefficient(double x, double y, double z, double s)
   { center[0] = x; center[1] = y; center[2] = z; scale = s; }
   const double *Center() { return center; }
   double Scale() { return scale; }
   virtual double Eval(ElementTransformation &T, const IntegrationPoint &ip)
   { mfem_error("DeltaCoefficient::Eval"); return 0.; }
   virtual void Read(istream &in) { }
   virtual ~DeltaCoefficient() { }
};

class VectorCoefficient
{
protected:
   int vdim;

public:

   VectorCoefficient (int vd) { vdim = vd; };

   /// Returns dimension of the vector.
   int GetVDim() { return vdim; };

   virtual void Eval (Vector &V, ElementTransformation &T,
                      const IntegrationPoint &ip) = 0;

   virtual ~VectorCoefficient() { };
};

class VectorFunctionCoefficient : public VectorCoefficient
{
private:
   void (*Function)(const Vector &, Vector &);
   Coefficient *Q;
   Vector transip;

public:
   VectorFunctionCoefficient (
      int dim, void (*F)(const Vector &, Vector &), Coefficient *q = NULL)
      : VectorCoefficient (dim), Q(q) { Function = F; };

   virtual void Eval (Vector &V, ElementTransformation &T,
                      const IntegrationPoint &ip);

   virtual ~VectorFunctionCoefficient() { };
};

/// Vector coefficient defined by an array of scalar coefficients.
class VectorArrayCoefficient : public VectorCoefficient
{
private:
   Array<Coefficient*> Coeff;

public:
   /// Construct vector of dim coefficients.
   VectorArrayCoefficient (int dim);

   /// Returns i'th coefficient.
   Coefficient & GetCoeff (int i) { return *Coeff[i]; }

   Coefficient ** GetCoeffs () { return Coeff; }

   /// Sets coefficient in the vector.
   void Set (int i, Coefficient * c) { Coeff[i] = c; }

   /// Evaluates i'th component of the vector.
   double Eval (int i, ElementTransformation &T, IntegrationPoint &ip)
   { return Coeff[i] -> Eval(T,ip); }

   virtual void Eval (Vector &V, ElementTransformation &T,
                      const IntegrationPoint &ip);

   /// Reads vector coefficient.
   void Read (int i, istream &in)  { Coeff[i] -> Read(in); }

   /// Destriys vector coefficient.
   virtual ~VectorArrayCoefficient();
};

/// Vector coefficient defined by a vector GridFunction
class VectorGridFunctionCoefficient : public VectorCoefficient
{
private:
   GridFunction *GridFunc;

public:
   VectorGridFunctionCoefficient (GridFunction *gf);

   virtual void Eval (Vector &V, ElementTransformation &T,
                      const IntegrationPoint &ip);

   virtual ~VectorGridFunctionCoefficient() { };
};


class MatrixCoefficient
{
protected:
   int vdim;

public:
   MatrixCoefficient (int dim) { vdim = dim; };

   int GetVDim() { return vdim; };

   virtual void Eval (DenseMatrix &K, ElementTransformation &T,
                      const IntegrationPoint &ip) = 0;

   virtual ~MatrixCoefficient() { };
};

class MatrixFunctionCoefficient : public MatrixCoefficient
{
private:
   void (*Function)(const Vector &, DenseMatrix &);
   Vector transip;

public:
   MatrixFunctionCoefficient (int dim, void (*F)(const Vector &,
                                                 DenseMatrix &))
      : MatrixCoefficient (dim) { Function = F; }

   virtual void Eval (DenseMatrix &K, ElementTransformation &T,
                      const IntegrationPoint &ip);

   virtual ~MatrixFunctionCoefficient() { }
};

class MatrixArrayCoefficient : public MatrixCoefficient
{
private:
   Array<Coefficient *> Coeff;

public:

   MatrixArrayCoefficient (int dim);

   Coefficient & GetCoeff (int i, int j) { return *Coeff[i*vdim+j]; }

   void Set (int i, int j, Coefficient * c) { Coeff[i*vdim+j] = c; }

   double Eval (int i, int j, ElementTransformation &T,
                IntegrationPoint &ip)
   { return Coeff[i*vdim+j] -> Eval(T, ip); }

   virtual void Eval (DenseMatrix &K, ElementTransformation &T,
                      const IntegrationPoint &ip);

   virtual ~MatrixArrayCoefficient();
};

#endif
