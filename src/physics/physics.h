#ifndef __PHYSICS__
#define __PHYSICS__

#include <deal.II/base/tensor.h>

#include "parameters/all_parameters.h"

namespace PHiLiP {
namespace Physics {

/// Base class from which Advection, Diffusion, ConvectionDiffusion, and Euler is derived.
/**
 *  Main interface for all the convective and diffusive terms.
 *
 *  LinearAdvection, Diffusion, ConvectionDiffusion, Euler are derived from this class.
 *
 *  Partial differential equation is given by the divergence of the convective and
 *  diffusive flux equal to the source term
 *
 *  \f[ \boldsymbol{\nabla} \cdot
 *         (  \mathbf{F}_{conv}( u ) 
 *          + \mathbf{F}_{diss}( u, \boldsymbol{\nabla}(u) )
 *      = s(\mathbf{x})
 *  \f]
 */
template <int dim, int nstate, typename real>
class PhysicsBase
{
public:
    /// Default constructor that will set the constants.
    PhysicsBase();

    /// Virtual destructor required for abstract classes.
    virtual ~PhysicsBase() = 0;

    /// Default manufactured solution.
    /** ~~~~~{.cpp}
     *  if (dim==1) uexact = sin(a*pos[0]+d);
     *  if (dim==2) uexact = sin(a*pos[0]+d)*sin(b*pos[1]+e);
     *  if (dim==3) uexact = sin(a*pos[0]+d)*sin(b*pos[1]+e)*sin(c*pos[2]+f);
     *  ~~~~~
     */
    virtual std::array<real,nstate> manufactured_solution (const dealii::Point<dim,double> &pos) const;

    /// Default manufactured solution gradient.
    virtual std::array<dealii::Tensor<1,dim,real>,nstate> manufactured_gradient (
        const dealii::Point<dim,double> &pos) const;

    /// Returns the integral of the manufactured solution over the hypercube [0,1].
    ///
    /// Either returns the linear output \f$\int u dV\f$
    /// or the nonlinear output \f$\int u^2 dV\f$.
    virtual double integral_output (const bool linear) const;

    /// Convective fluxes that will be differentiated once in space.
    virtual std::array<dealii::Tensor<1,dim,real>,nstate> convective_flux (
        const std::array<real,nstate> &solution) const = 0;

    /// Spectral radius of convective term Jacobian.
    /// Used for scalar dissipation
    virtual std::array<real,nstate> convective_eigenvalues (
        const std::array<real,nstate> &/*solution*/,
        const dealii::Tensor<1,dim,real> &/*normal*/) const = 0;

    /// Maximum convective eigenvalue used in Lax-Friedrichs
    virtual real max_convective_eigenvalue (const std::array<real,nstate> &soln) const = 0;

    // /// Evaluate the diffusion matrix \f$ A \f$ such that \f$F_v = A \nabla u\f$.
    // virtual std::array<dealii::Tensor<1,dim,real>,nstate> apply_diffusion_matrix (
    //     const std::array<real,nstate> &solution,
    //     const std::array<dealii::Tensor<1,dim,real>,nstate> &solution_grad) const = 0;

    /// Dissipative fluxes that will be differentiated once in space.
    /// Evaluates the dissipative flux through the linearization F = A(u)*grad(u).
    std::array<dealii::Tensor<1,dim,real>,nstate> dissipative_flux_A_gradu (
        const real scaling,
        const std::array<real,nstate> &solution,
        const std::array<dealii::Tensor<1,dim,real>,nstate> &solution_gradient,
        std::array<dealii::Tensor<1,dim,real>,nstate> &diss_flux) const;

    /// Dissipative fluxes that will be differentiated once in space.
    virtual std::array<dealii::Tensor<1,dim,real>,nstate> dissipative_flux (
        const std::array<real,nstate> &solution,
        const std::array<dealii::Tensor<1,dim,real>,nstate> &solution_gradient) const = 0;

    /// Source term that does not require differentiation.
    virtual std::array<real,nstate> source_term (
        const dealii::Point<dim,double> &pos,
        const std::array<real,nstate> &solution) const = 0;

    /// Evaluates boundary values and gradients on the other side of the face.
    virtual void boundary_face_values (
        const int /*boundary_type*/,
        const dealii::Point<dim, double> &/*pos*/,
        const dealii::Tensor<1,dim,real> &/*normal*/,
        const std::array<real,nstate> &/*soln_int*/,
        const std::array<dealii::Tensor<1,dim,real>,nstate> &/*soln_grad_int*/,
        std::array<real,nstate> &/*soln_bc*/,
        std::array<dealii::Tensor<1,dim,real>,nstate> &/*soln_grad_bc*/) const;
protected:
    /// Not yet implemented
    virtual void set_manufactured_dirichlet_boundary_condition (
        const std::array<real,nstate> &/*soln_int*/,
        const std::array<dealii::Tensor<1,dim,real>,nstate> &/*soln_grad_int*/,
        std::array<real,nstate> &/*soln_bc*/,
        std::array<dealii::Tensor<1,dim,real>,nstate> &/*soln_grad_bc*/) const;
    /// Not yet implemented
    virtual void set_manufactured_neumann_boundary_condition (
        const std::array<real,nstate> &/*soln_int*/,
        const std::array<dealii::Tensor<1,dim,real>,nstate> &/*soln_grad_int*/,
        std::array<real,nstate> &/*soln_bc*/,
        std::array<dealii::Tensor<1,dim,real>,nstate> &/*soln_grad_bc*/) const;


    /// Some constants used to define manufactured solution
    double freq_x, freq_y, freq_z;
    double offs_x, offs_y, offs_z;
    double velo_x, velo_y, velo_z;
    double diff_coeff;

    /// Heterogeneous diffusion matrix
    /** As long as the diagonal components are positive and diagonally dominant
     *  we should have a stable diffusive system
     */
    double A11, A12, A13;
    double A21, A22, A23;
    double A31, A32, A33;
};

/// Create specified physics as PhysicsBase object 
/** Factory design pattern whose job is to create the correct physics
 */
template <int dim, int nstate, typename real>
class PhysicsFactory
{
public:
    static PhysicsBase<dim,nstate,real>*
        create_Physics(Parameters::AllParameters::PartialDifferentialEquation pde_type);
};

/// Convection-diffusion with linear advective and diffusive term.  Derived from PhysicsBase.
/** State variable: \f$ u \f$
 *  
 *  Convective flux \f$ \mathbf{F}_{conv} =  u \f$
 *
 *  Dissipative flux \f$ \mathbf{F}_{diss} = -\boldsymbol\nabla u \f$
 *
 *  Source term \f$ s(\mathbf{x}) \f$
 *
 *  Equation:
 *  \f[ \boldsymbol{\nabla} \cdot
 *         (  \mathbf{F}_{conv}( u ) 
 *          + \mathbf{F}_{diss}( u, \boldsymbol{\nabla}(u) )
 *      = s(\mathbf{x})
 *  \f]
 */
template <int dim, int nstate, typename real>
class ConvectionDiffusion : public PhysicsBase <dim, nstate, real>
{
public:
    const bool hasConvection;
    const bool hasDiffusion;
    /// Constructor
    ConvectionDiffusion (const bool convection = true, const bool diffusion = true)
        : hasConvection(convection), hasDiffusion(diffusion)
    {
        static_assert(nstate<=2, "Physics::ConvectionDiffusion() should be created with nstate<=2");
    };

    /// Destructor
    ~ConvectionDiffusion () {};
    /// Convective flux: \f$ \mathbf{F}_{conv} =  u \f$
    std::array<dealii::Tensor<1,dim,real>,nstate> convective_flux (const std::array<real,nstate> &solution) const;

    /// Spectral radius of convective term Jacobian is 'c'
    std::array<real,nstate> convective_eigenvalues (
        const std::array<real,nstate> &/*solution*/,
        const dealii::Tensor<1,dim,real> &/*normal*/) const;

    /// Maximum convective eigenvalue used in Lax-Friedrichs
    real max_convective_eigenvalue (const std::array<real,nstate> &soln) const;

    //  /// Diffusion matrix is identity
    //  std::array<dealii::Tensor<1,dim,real>,nstate> apply_diffusion_matrix (
    //      const std::array<real,nstate> &solution,
    //      const std::array<dealii::Tensor<1,dim,real>,nstate> &solution_grad) const;

    /// Dissipative flux: u
    std::array<dealii::Tensor<1,dim,real>,nstate> dissipative_flux (
        const std::array<real,nstate> &solution,
        const std::array<dealii::Tensor<1,dim,real>,nstate> &solution_gradient) const;

    /// Source term is zero or depends on manufactured solution
    std::array<real,nstate> source_term (
        const dealii::Point<dim,double> &pos,
        const std::array<real,nstate> &solution) const;

    void boundary_face_values (
        const int /*boundary_type*/,
        const dealii::Point<dim, double> &/*pos*/,
        const dealii::Tensor<1,dim,real> &/*normal*/,
        const std::array<real,nstate> &/*soln_int*/,
        const std::array<dealii::Tensor<1,dim,real>,nstate> &/*soln_grad_int*/,
        std::array<real,nstate> &/*soln_bc*/,
        std::array<dealii::Tensor<1,dim,real>,nstate> &/*soln_grad_bc*/) const;

protected:
    /// Linear advection speed:  c
    dealii::Tensor<1,dim,real> advection_speed () const;
    /// Diffusion coefficient
    real diffusion_coefficient () const;
};

/// Euler equations. Derived from PhysicsBase
/** Only 2D and 3D
 *  State variable and convective fluxes given by
 *
 *  \f[ 
 *  \mathbf{u} = 
 *  \begin{bmatrix} \rho \\ \rho v_1 \\ \rho v_2 \\ \rho v_3 \\ e \end{bmatrix}
 *  , \qquad
 *  \mathbf{F}_{conv} = 
 *  \begin{bmatrix} 
 *      \mathbf{f}^x_{conv}, \mathbf{f}^y_{conv}, \mathbf{f}^z_{conv}
 *  \end{bmatrix}
 *  =
 *  \begin{bmatrix} 
 *  \begin{bmatrix} 
 *  \rho v_1 \\
 *  \rho v_1 v_1 + p \\
 *  \rho v_1 v_2     \\ 
 *  \rho v_1 v_3     \\
 *  v_1 (e+p)
 *  \end{bmatrix}
 *  ,
 *  \begin{bmatrix} 
 *  \rho v_2 \\
 *  \rho v_1 v_2     \\
 *  \rho v_2 v_2 + p \\ 
 *  \rho v_2 v_3     \\
 *  v_2 (e+p)
 *  \end{bmatrix}
 *  ,
 *  \begin{bmatrix} 
 *  \rho v_3 \\
 *  \rho v_1 v_3     \\
 *  \rho v_2 v_3     \\ 
 *  \rho v_3 v_3 + p \\
 *  v_3 (e+p)
 *  \end{bmatrix}
 *  \end{bmatrix} \f]
 *  
 *  For a calorically perfect gas
 *
 *  \f[
 *  p=(\gamma -1)(e-\frac{1}{2}\rho \|\mathbf{v}\|)
 *  \f]
 *
 *  Dissipative flux \f$ \mathbf{F}_{diss} = \mathbf{0} \f$
 *
 *  Source term \f$ s(\mathbf{x}) \f$
 *
 *  Equation:
 *  \f[ \boldsymbol{\nabla} \cdot
 *         (  \mathbf{F}_{conv}( u ) 
 *          + \mathbf{F}_{diss}( u, \boldsymbol{\nabla}(u) )
 *      = s(\mathbf{x})
 *  \f]
 */
template <int dim, int nstate, typename real>
class Euler : public PhysicsBase <dim, nstate, real>
{
public:
    /// Constructor
    Euler ()
    {
        static_assert(nstate==dim+2, "Physics::Euler() should be created with nstate=dim+2");
    };
    /// Destructor
    ~Euler ()
    {};

    /// Manufactured solution for Euler
    std::array<real,nstate> manufactured_solution (const dealii::Point<dim,double> &pos) const;

    /// Convective flux: \f$ \mathbf{F}_{conv} =  u \f$
    std::array<dealii::Tensor<1,dim,real>,nstate> convective_flux (
        const std::array<real,nstate> &conservative_soln) const;

    /// Spectral radius of convective term Jacobian is 'c'
    std::array<real,nstate> convective_eigenvalues (
        const std::array<real,nstate> &/*conservative_soln*/,
        const dealii::Tensor<1,dim,real> &/*normal*/) const;

    /// Maximum convective eigenvalue used in Lax-Friedrichs
    real max_convective_eigenvalue (const std::array<real,nstate> &soln) const;

    //  /// Diffusion matrix is identity
    //  std::array<dealii::Tensor<1,dim,real>,nstate> apply_diffusion_matrix (
    //      const std::array<real,nstate> &conservative_soln,
    //      const std::array<dealii::Tensor<1,dim,real>,nstate> &solution_grad) const;

    /// Dissipative flux: u
    std::array<dealii::Tensor<1,dim,real>,nstate> dissipative_flux (
        const std::array<real,nstate> &conservative_soln,
        const std::array<dealii::Tensor<1,dim,real>,nstate> &solution_gradient) const;

    /// Source term is zero or depends on manufactured solution
    std::array<real,nstate> source_term (
        const dealii::Point<dim,double> &pos,
        const std::array<real,nstate> &conservative_soln) const;

    /// Given conservative variables [density, [momentum], total energy],
    /// returns primitive variables [density, [velocities], pressure].
    ///
    /// Opposite of convert_primitive_to_conservative
    std::array<real,nstate> convert_conservative_to_primitive ( const std::array<real,nstate> &conservative_soln ) const;

    /// Given primitive variables [density, [velocities], pressure],
    /// returns conservative variables [density, [momentum], total energy].
    ///
    /// Opposite of convert_primitive_to_conservative
    std::array<real,nstate> convert_primitive_to_conservative ( const std::array<real,nstate> &primitive_soln ) const;

protected:
    /// Constant heat capacity ratio of air
    const real gam = 1.4;
    /// Evaluate pressure from conservative variables
    real compute_pressure ( const std::array<real,nstate> &conservative_soln ) const;
    /// Evaluate speed of sound from conservative variables
    real compute_sound ( const std::array<real,nstate> &conservative_soln ) const;
    /// Evaluate velocities from conservative variables
    std::array<real,dim> compute_velocities ( const std::array<real,nstate> &conservative_soln ) const;
    /// Given the velocity vector \f$ \mathbf{u} \f$, returns the dot-product  \f$ \mathbf{u} \cdot \mathbf{u} \f$
    real compute_velocity_squared ( const std::array<real,dim> &velocities ) const;

    /// Given primitive variables, returns velocities.
    std::array<real,dim> extract_velocities_from_primitive ( const std::array<real,nstate> &primitive_soln ) const;
    /// Given primitive variables, returns total energy
    real compute_energy ( const std::array<real,nstate> &primitive_soln ) const;


};

} // Physics namespace
} // PHiLiP namespace

#endif
