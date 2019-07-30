#include <deal.II/base/tensor.h>

#include <deal.II/fe/fe_values.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/lac/vector.h>

#include <Sacado.hpp>
//#include <deal.II/differentiation/ad/sacado_math.h>
//#include <deal.II/differentiation/ad/sacado_number_types.h>
#include <deal.II/differentiation/ad/sacado_product_types.h>

#include <deal.II/fe/fe_dgq.h> // Used for flux interpolation

#include "dg.h"
#include "physics/physics_factory.h"

namespace PHiLiP {


template <int dim, int nstate, typename real>
DGStrong<dim,nstate,real>::DGStrong(
    const Parameters::AllParameters *const parameters_input,
    const unsigned int degree)
    : DGBase<dim,real>::DGBase(nstate, parameters_input, degree) // Use DGBase constructor
{
    using ADtype = Sacado::Fad::DFad<real>;
    pde_physics = Physics::PhysicsFactory<dim,nstate,ADtype> 
        ::create_Physics(parameters_input);
    conv_num_flux = NumericalFlux::NumericalFluxFactory<dim, nstate, ADtype>
        ::create_convective_numerical_flux (parameters_input->conv_num_flux_type, pde_physics);
    diss_num_flux = NumericalFlux::NumericalFluxFactory<dim, nstate, ADtype>
        ::create_dissipative_numerical_flux (parameters_input->diss_num_flux_type, pde_physics);
    split_fluxes = SplitFormFactory<dim, nstate, ADtype>
        ::create_SplitForm(parameters_input->pde_type);
}

template <int dim, int nstate, typename real>
DGStrong<dim,nstate,real>::~DGStrong ()
{ 
    std::cout << "Destructing DGStrong..." << std::endl;
    delete conv_num_flux;
    delete diss_num_flux;
}

template <int dim, int nstate, typename real>
void DGStrong<dim,nstate,real>::assemble_cell_terms_implicit(
    const dealii::FEValues<dim,dim> &fe_values_vol,
    const std::vector<dealii::types::global_dof_index> &cell_dofs_indices,
    dealii::Vector<real> &local_rhs_int_cell)
{

    using ADtype = Sacado::Fad::DFad<real>;
    using ADArray = std::array<ADtype,nstate>;
    using ADArrayTensor1 = std::array< dealii::Tensor<1,dim,ADtype>, nstate >;

    const unsigned int n_quad_pts      = fe_values_vol.n_quadrature_points;
    const unsigned int n_dofs_cell     = fe_values_vol.dofs_per_cell;

    AssertDimension (n_dofs_cell, cell_dofs_indices.size());

    const std::vector<real> &JxW = fe_values_vol.get_JxW_values ();


    std::vector<real> residual_derivatives(n_dofs_cell);

    std::vector< ADArray > soln_at_q(n_quad_pts);
    std::vector< ADArrayTensor1 > soln_grad_at_q(n_quad_pts); // Tensor initialize with zeros

    std::vector< ADArrayTensor1 > conv_phys_flux_at_q(n_quad_pts);
    std::vector< ADArrayTensor1 > diss_phys_flux_at_q(n_quad_pts);
    std::vector< ADArray > source_at_q(n_quad_pts);

    //vector containing split forms;
    std::vector<ADArrayTensor1> conv_split_phys_flux_at_q(n_quad_pts);


    // AD variable
    std::vector< ADtype > soln_coeff(n_dofs_cell);
    for (unsigned int idof = 0; idof < n_dofs_cell; ++idof) {
        soln_coeff[idof] = DGBase<dim,real>::solution(cell_dofs_indices[idof]);
        soln_coeff[idof].diff(idof, n_dofs_cell);
    }
    for (unsigned int iquad=0; iquad<n_quad_pts; ++iquad) {
        for (int istate=0; istate<nstate; istate++) { 
            // Interpolate solution to the volume quadrature points
            soln_at_q[iquad][istate]      = 0;
            soln_grad_at_q[iquad][istate] = 0;
        }
    }
    // Interpolate solution to face
    for (unsigned int iquad=0; iquad<n_quad_pts; ++iquad) {
        for (unsigned int idof=0; idof<n_dofs_cell; ++idof) {
              const unsigned int istate = fe_values_vol.get_fe().system_to_component_index(idof).first;
              soln_at_q[iquad][istate]      += soln_coeff[idof] * fe_values_vol.shape_value_component(idof, iquad, istate);
              soln_grad_at_q[iquad][istate] += soln_coeff[idof] * fe_values_vol.shape_grad_component(idof, iquad, istate);
        }
        //std::cout << "Density " << soln_at_q[iquad][0] << std::endl;
        //if(nstate>1) std::cout << "Momentum " << soln_at_q[iquad][1] << std::endl;
        //std::cout << "Energy " << soln_at_q[iquad][nstate-1] << std::endl;
        // Evaluate physical convective flux and source term
        conv_phys_flux_at_q[iquad] = pde_physics->convective_flux (soln_at_q[iquad]);
        diss_phys_flux_at_q[iquad] = pde_physics->dissipative_flux (soln_at_q[iquad], soln_grad_at_q[iquad]);

        //THIS IS WHERE I NEED TO EVALUATE SPLIT FORM FLUXES

        if(this->all_parameters->manufactured_convergence_study_param.use_manufactured_source_term) {
            source_at_q[iquad] = pde_physics->source_term (fe_values_vol.quadrature_point(iquad), soln_at_q[iquad]);
        }
    }


    // Evaluate flux divergence by interpolating the flux
    // Since we have nodal values of the flux, we use the Lagrange polynomials to obtain the gradients at the quadrature points.
    dealii::FE_DGQArbitraryNodes<dim,dim> lagrange_poly(this->oned_quadrature);
    dealii::FEValues<dim,dim> fe_values_lagrange (this->mapping, lagrange_poly, this->volume_quadrature, this->update_flags);
    fe_values_lagrange.reinit(fe_values_vol.get_cell());
    std::vector<ADArray> flux_divergence(n_quad_pts);
    std::vector<ADtype> f1(n_quad_pts);
    std::vector<ADtype> f2(n_quad_pts);
    std::vector<ADtype> g1(n_quad_pts);
    std::vector<ADtype> g2(n_quad_pts);
    for (int istate = 0; istate<nstate; ++istate) {
        for (unsigned int iquad=0; iquad<n_quad_pts; ++iquad) {
            flux_divergence[iquad][istate] = 0.0;
            f1[iquad]=0;
            f2[iquad]=0;
            g1[iquad]=0;
            g2[iquad]=0;
            for ( unsigned int flux_basis = 0; flux_basis < n_quad_pts; ++flux_basis ) {
                flux_divergence[iquad][istate] += conv_phys_flux_at_q[flux_basis][istate] * fe_values_lagrange.shape_grad(flux_basis,iquad);
                //std::cout << split_fluxes->split_convective_fluxes[0][0][1].g(soln_at_q[flux_basis]) << " vs " << conv_phys_flux_at_q[flux_basis][istate] << std::endl;
                //std::cout << fe_values_vol.shape_grad(flux_basis,iquad)[0] << " VS " << fe_values_lagrange.shape_grad(flux_basis,iquad) << std::endl;
                //abtin_divergence[iquad][istate] += split_fluxes->split_convective_fluxes[0][0][1].g(soln_at_q[flux_basis]) * fe_values_vol.shape_grad(flux_basis,istate)[0];
                f1[iquad] += split_fluxes->split_convective_fluxes[0][0][0].f(soln_at_q[flux_basis]) * fe_values_vol.shape_grad_component(flux_basis,iquad, istate)[0];
                f2[iquad] += split_fluxes->split_convective_fluxes[0][0][1].f(soln_at_q[flux_basis]) * fe_values_vol.shape_grad_component(flux_basis,iquad, istate)[0];
                g1[iquad] += split_fluxes->split_convective_fluxes[0][0][0].g(soln_at_q[flux_basis]) * fe_values_vol.shape_grad_component(flux_basis,iquad, istate)[0];
                g2[iquad] += split_fluxes->split_convective_fluxes[0][0][1].g(soln_at_q[flux_basis]) * fe_values_vol.shape_grad_component(flux_basis,iquad, istate)[0];
                //std:: cout << " flux " << split_fluxes->split_convective_fluxes[0][0][1].g(soln_at_q[flux_state]) << " shapegrad "
                //		   << fe_values_vol.shape_grad_component(flux_basis, iquad, istate)[0]
				//		   << " soln " << soln_at_q[iquad][0]
				//		   << std::endl;
               // std::cout << g2[iquad] << std::endl;

            }
           // std::cout << g2[iquad].val() << std::endl;

        }
    }

    // Strong form
    // The right-hand side sends all the term to the side of the source term
    // Therefore, 
    // \divergence ( Fconv + Fdiss ) = source 
    // has the right-hand side
    // rhs = - \divergence( Fconv + Fdiss ) + source 
    // Since we have done an integration by parts, the volume term resulting from the divergence of Fconv and Fdiss
    // is negative. Therefore, negative of negative means we add that volume term to the right-hand-side
    for (unsigned int itest=0; itest<n_dofs_cell; ++itest) {

        ADtype rhs = 0;


        const unsigned int istate = fe_values_vol.get_fe().system_to_component_index(itest).first;

        if (this->all_parameters->use_split_form)
        {
        	ADtype inter1 = 0;
        	ADtype inter2 = 0;
        	for (unsigned int iquad = 0; iquad < n_quad_pts; ++iquad)
        	{
        		inter2 = inter2 + fe_values_vol.shape_value_component(itest,iquad,istate) *  g2[iquad] * JxW[iquad];
        		inter1 = inter1 + fe_values_vol.shape_value_component(itest,iquad,istate) * g1[iquad] * JxW[iquad];
        	}

        	rhs = rhs - inter1 * split_fluxes->split_convective_fluxes[0][0][0].f(soln_at_q[itest]) * split_fluxes->split_convective_fluxes[0][0][0].alpha;
        	rhs = rhs - inter2 * split_fluxes->split_convective_fluxes[0][0][1].f(soln_at_q[itest]) * split_fluxes->split_convective_fluxes[0][0][1].alpha;

//        	assert(this->all_parameters->use_collocated_nodes == true && "Nodes aren't collocated, can't use split form");
//        	ADtype intermediate1 = 0;
//        	for (int idim = 0; idim < dim ; ++idim)
//        	{
//        		for (unsigned int isplit = 0; isplit < split_fluxes->split_convective_fluxes[idim][istate].size(); ++isplit)
//        		{
//        			for (unsigned int iquad = 0; iquad < n_quad_pts; ++iquad)
//        			{
//        				intermediate1 = intermediate1 + fe_values_vol.shape_grad_component(itest,iquad,istate)[idim] * split_fluxes->split_convective_fluxes[idim][istate][isplit].g(soln_at_q[iquad]);
//        			}
//
//        			rhs = rhs - intermediate1 * split_fluxes->split_convective_fluxes[idim][istate][isplit].alpha * split_fluxes->split_convective_fluxes[idim][istate][isplit].f(soln_at_q[itest]) * JxW[itest];
//        			intermediate1 = 0;
//        		}
//        	}
        }

        for (unsigned int iquad=0; iquad<n_quad_pts; ++iquad) {

            // Convective
            // Now minus such 2 integrations by parts
            assert(JxW[iquad] - fe_values_lagrange.JxW(iquad) < 1e-14);

            if (this->all_parameters->use_split_form == false)
            	rhs = rhs - fe_values_vol.shape_value_component(itest,iquad,istate) * flux_divergence[iquad][istate] * JxW[iquad];

            //// Diffusive
            //// Note that for diffusion, the negative is defined in the physics
            rhs = rhs + fe_values_vol.shape_grad_component(itest,iquad,istate) * diss_phys_flux_at_q[iquad][istate] * JxW[iquad];
            // Source

            if(this->all_parameters->manufactured_convergence_study_param.use_manufactured_source_term) {
                rhs = rhs + fe_values_vol.shape_value_component(itest,iquad,istate) * source_at_q[iquad][istate] * JxW[iquad];
            }
        }

        local_rhs_int_cell(itest) += rhs.val();

        if (this->all_parameters->ode_solver_param.ode_solver_type == Parameters::ODESolverParam::ODESolverEnum::implicit_solver) {
            for (unsigned int idof = 0; idof < n_dofs_cell; ++idof) {
                //residual_derivatives[idof] = rhs.fastAccessDx(idof);
                residual_derivatives[idof] = rhs.dx(idof);
            }
            this->system_matrix.add(cell_dofs_indices[itest], cell_dofs_indices, residual_derivatives);
        }
    }
}


template <int dim, int nstate, typename real>
void DGStrong<dim,nstate,real>::assemble_boundary_term_implicit(
    const unsigned int boundary_id,
    const dealii::FEFaceValues<dim,dim> &fe_values_boundary,
    const real penalty,
    const std::vector<dealii::types::global_dof_index> &dof_indices_int,
    dealii::Vector<real> &local_rhs_int_cell)
{
    using ADtype = Sacado::Fad::DFad<real>;
    using ADArray = std::array<ADtype,nstate>;
    using ADArrayTensor1 = std::array< dealii::Tensor<1,dim,ADtype>, nstate >;

    const unsigned int n_dofs_cell = fe_values_boundary.dofs_per_cell;
    const unsigned int n_face_quad_pts = fe_values_boundary.n_quadrature_points;

    AssertDimension (n_dofs_cell, dof_indices_int.size());

    const std::vector<real> &JxW = fe_values_boundary.get_JxW_values ();
    const std::vector<dealii::Tensor<1,dim>> &normals = fe_values_boundary.get_normal_vectors ();

    std::vector<real> residual_derivatives(n_dofs_cell);

    std::vector<ADArray> soln_int(n_face_quad_pts);
    std::vector<ADArray> soln_ext(n_face_quad_pts);

    std::vector<ADArrayTensor1> soln_grad_int(n_face_quad_pts);
    std::vector<ADArrayTensor1> soln_grad_ext(n_face_quad_pts);

    std::vector<ADArray> conv_num_flux_dot_n(n_face_quad_pts);
    std::vector<ADArray> diss_soln_num_flux(n_face_quad_pts); // u*
    std::vector<ADArrayTensor1> diss_flux_jump_int(n_face_quad_pts); // u*-u_int
    std::vector<ADArray> diss_auxi_num_flux_dot_n(n_face_quad_pts); // sigma*

    std::vector<ADArrayTensor1> conv_phys_flux(n_face_quad_pts);

    // AD variable
    std::vector< ADtype > soln_coeff_int(n_dofs_cell);
    const unsigned int n_total_indep = n_dofs_cell;
    for (unsigned int idof = 0; idof < n_dofs_cell; ++idof) {
        soln_coeff_int[idof] = DGBase<dim,real>::solution(dof_indices_int[idof]);
        soln_coeff_int[idof].diff(idof, n_total_indep);
    }

    for (unsigned int iquad=0; iquad<n_face_quad_pts; ++iquad) {
        for (int istate=0; istate<nstate; istate++) { 
            // Interpolate solution to the face quadrature points
            soln_int[iquad][istate]      = 0;
            soln_grad_int[iquad][istate] = 0;
        }
    }
    // Interpolate solution to face
    const std::vector< dealii::Point<dim,real> > quad_pts = fe_values_boundary.get_quadrature_points();
    for (unsigned int iquad=0; iquad<n_face_quad_pts; ++iquad) {

        const dealii::Tensor<1,dim,ADtype> normal_int = normals[iquad];
        const dealii::Tensor<1,dim,ADtype> normal_ext = -normal_int;

        for (unsigned int idof=0; idof<n_dofs_cell; ++idof) {
            const int istate = fe_values_boundary.get_fe().system_to_component_index(idof).first;
            soln_int[iquad][istate]      += soln_coeff_int[idof] * fe_values_boundary.shape_value_component(idof, iquad, istate);
            soln_grad_int[iquad][istate] += soln_coeff_int[idof] * fe_values_boundary.shape_grad_component(idof, iquad, istate);
        }

        const dealii::Point<dim, real> x_quad = quad_pts[iquad];
        pde_physics->boundary_face_values (boundary_id, x_quad, normal_int, soln_int[iquad], soln_grad_int[iquad], soln_ext[iquad], soln_grad_ext[iquad]);

        //
        // Evaluate physical convective flux, physical dissipative flux
        // Following the the boundary treatment given by 
        //      Hartmann, R., Numerical Analysis of Higher Order Discontinuous Galerkin Finite Element Methods,
        //      Institute of Aerodynamics and Flow Technology, DLR (German Aerospace Center), 2008.
        //      Details given on page 93
        //conv_num_flux_dot_n[iquad] = conv_num_flux->evaluate_flux(soln_ext[iquad], soln_ext[iquad], normal_int);

        // So, I wasn't able to get Euler manufactured solutions to converge when F* = F*(Ubc, Ubc)
        // Changing it back to the standdard F* = F*(Uin, Ubc)
        // This is known not be adjoint consistent as per the paper above. Page 85, second to last paragraph.
        // Losing 2p+1 OOA on functionals for all PDEs.
        conv_num_flux_dot_n[iquad] = conv_num_flux->evaluate_flux(soln_int[iquad], soln_ext[iquad], normal_int);

        // Used for strong form
        // Which physical convective flux to use?
        conv_phys_flux[iquad] = pde_physics->convective_flux (soln_int[iquad]);

        // Notice that the flux uses the solution given by the Dirichlet or Neumann boundary condition
        diss_soln_num_flux[iquad] = diss_num_flux->evaluate_solution_flux(soln_ext[iquad], soln_ext[iquad], normal_int);

        ADArrayTensor1 diss_soln_jump_int;
        for (int s=0; s<nstate; s++) {
            diss_soln_jump_int[s] = (diss_soln_num_flux[iquad][s] - soln_int[iquad][s]) * normal_int;
        }
        diss_flux_jump_int[iquad] = pde_physics->dissipative_flux (soln_int[iquad], diss_soln_jump_int);

        diss_auxi_num_flux_dot_n[iquad] = diss_num_flux->evaluate_auxiliary_flux(
            soln_int[iquad], soln_ext[iquad],
            soln_grad_int[iquad], soln_grad_ext[iquad],
            normal_int, penalty, true);
    }

    // Boundary integral
    for (unsigned int itest=0; itest<n_dofs_cell; ++itest) {

        ADtype rhs = 0.0;

        const unsigned int istate = fe_values_boundary.get_fe().system_to_component_index(itest).first;

        for (unsigned int iquad=0; iquad<n_face_quad_pts; ++iquad) {

            // Convection
            const ADtype flux_diff = conv_num_flux_dot_n[iquad][istate] - conv_phys_flux[iquad][istate]*normals[iquad];
            rhs = rhs - fe_values_boundary.shape_value_component(itest,iquad,istate) * flux_diff * JxW[iquad];
            // Diffusive
            rhs = rhs - fe_values_boundary.shape_value_component(itest,iquad,istate) * diss_auxi_num_flux_dot_n[iquad][istate] * JxW[iquad];
            rhs = rhs + fe_values_boundary.shape_grad_component(itest,iquad,istate) * diss_flux_jump_int[iquad][istate] * JxW[iquad];
        }
        // *******************

        local_rhs_int_cell(itest) += rhs.val();

        if (this->all_parameters->ode_solver_param.ode_solver_type == Parameters::ODESolverParam::ODESolverEnum::implicit_solver) {
            for (unsigned int idof = 0; idof < n_dofs_cell; ++idof) {
                //residual_derivatives[idof] = rhs.fastAccessDx(idof);
                residual_derivatives[idof] = rhs.dx(idof);
            }
            this->system_matrix.add(dof_indices_int[itest], dof_indices_int, residual_derivatives);
        }
    }
}

template <int dim, int nstate, typename real>
void DGStrong<dim,nstate,real>::assemble_face_term_implicit(
    const dealii::FEValuesBase<dim,dim>     &fe_values_int,
    const dealii::FEFaceValues<dim,dim>     &fe_values_ext,
    const real penalty,
    const std::vector<dealii::types::global_dof_index> &dof_indices_int,
    const std::vector<dealii::types::global_dof_index> &dof_indices_ext,
    dealii::Vector<real>          &local_rhs_int_cell,
    dealii::Vector<real>          &local_rhs_ext_cell)
{
    using ADtype = Sacado::Fad::DFad<real>;
    using ADArray = std::array<ADtype,nstate>;
    using ADArrayTensor1 = std::array< dealii::Tensor<1,dim,ADtype>, nstate >;

    // Use quadrature points of neighbor cell
    // Might want to use the maximum n_quad_pts1 and n_quad_pts2
    const unsigned int n_face_quad_pts = fe_values_ext.n_quadrature_points;

    const unsigned int n_dofs_int = fe_values_int.dofs_per_cell;
    const unsigned int n_dofs_ext = fe_values_ext.dofs_per_cell;

    AssertDimension (n_dofs_int, dof_indices_int.size());
    AssertDimension (n_dofs_ext, dof_indices_ext.size());

    // Jacobian and normal should always be consistent between two elements
    // even for non-conforming meshes?
    const std::vector<real> &JxW_int = fe_values_int.get_JxW_values ();
    const std::vector<dealii::Tensor<1,dim> > &normals_int = fe_values_int.get_normal_vectors ();

    // AD variable
    std::vector<ADtype> soln_coeff_int_ad(n_dofs_int);
    std::vector<ADtype> soln_coeff_ext_ad(n_dofs_ext);


    // Jacobian blocks
    std::vector<real> dR1_dW1(n_dofs_int);
    std::vector<real> dR1_dW2(n_dofs_ext);
    std::vector<real> dR2_dW1(n_dofs_int);
    std::vector<real> dR2_dW2(n_dofs_ext);

    std::vector<ADArray> conv_num_flux_dot_n(n_face_quad_pts);
    std::vector<ADArrayTensor1> conv_phys_flux_int(n_face_quad_pts);
    std::vector<ADArrayTensor1> conv_phys_flux_ext(n_face_quad_pts);

    // Interpolate solution to the face quadrature points
    std::vector< ADArray > soln_int(n_face_quad_pts);
    std::vector< ADArray > soln_ext(n_face_quad_pts);

    std::vector< ADArrayTensor1 > soln_grad_int(n_face_quad_pts); // Tensor initialize with zeros
    std::vector< ADArrayTensor1 > soln_grad_ext(n_face_quad_pts); // Tensor initialize with zeros

    std::vector<ADArray> diss_soln_num_flux(n_face_quad_pts); // u*
    std::vector<ADArray> diss_auxi_num_flux_dot_n(n_face_quad_pts); // sigma*

    std::vector<ADArrayTensor1> diss_flux_jump_int(n_face_quad_pts); // u*-u_int
    std::vector<ADArrayTensor1> diss_flux_jump_ext(n_face_quad_pts); // u*-u_ext
    // AD variable
    const unsigned int n_total_indep = n_dofs_int + n_dofs_ext;
    for (unsigned int idof = 0; idof < n_dofs_int; ++idof) {
        soln_coeff_int_ad[idof] = DGBase<dim,real>::solution(dof_indices_int[idof]);
        soln_coeff_int_ad[idof].diff(idof, n_total_indep);
    }
    for (unsigned int idof = 0; idof < n_dofs_ext; ++idof) {
        soln_coeff_ext_ad[idof] = DGBase<dim,real>::solution(dof_indices_ext[idof]);
        soln_coeff_ext_ad[idof].diff(idof+n_dofs_int, n_total_indep);
    }
    for (unsigned int iquad=0; iquad<n_face_quad_pts; ++iquad) {
        for (int istate=0; istate<nstate; istate++) { 
            soln_int[iquad][istate]      = 0;
            soln_grad_int[iquad][istate] = 0;
            soln_ext[iquad][istate]      = 0;
            soln_grad_ext[iquad][istate] = 0;
        }
    }
    for (unsigned int iquad=0; iquad<n_face_quad_pts; ++iquad) {

        const dealii::Tensor<1,dim,ADtype> normal_int = normals_int[iquad];
        const dealii::Tensor<1,dim,ADtype> normal_ext = -normal_int;

        // Interpolate solution to face
        for (unsigned int idof=0; idof<n_dofs_int; ++idof) {
            const unsigned int istate = fe_values_int.get_fe().system_to_component_index(idof).first;
            soln_int[iquad][istate]      += soln_coeff_int_ad[idof] * fe_values_int.shape_value_component(idof, iquad, istate);
            soln_grad_int[iquad][istate] += soln_coeff_int_ad[idof] * fe_values_int.shape_grad_component(idof, iquad, istate);
        }
        for (unsigned int idof=0; idof<n_dofs_ext; ++idof) {
            const unsigned int istate = fe_values_ext.get_fe().system_to_component_index(idof).first;
            soln_ext[iquad][istate]      += soln_coeff_ext_ad[idof] * fe_values_ext.shape_value_component(idof, iquad, istate);
            soln_grad_ext[iquad][istate] += soln_coeff_ext_ad[idof] * fe_values_ext.shape_grad_component(idof, iquad, istate);
        }
        //std::cout << "Density int" << soln_int[iquad][0] << std::endl;
        //if(nstate>1) std::cout << "Momentum int" << soln_int[iquad][1] << std::endl;
        //std::cout << "Energy int" << soln_int[iquad][nstate-1] << std::endl;
        //std::cout << "Density ext" << soln_ext[iquad][0] << std::endl;
        //if(nstate>1) std::cout << "Momentum ext" << soln_ext[iquad][1] << std::endl;
        //std::cout << "Energy ext" << soln_ext[iquad][nstate-1] << std::endl;

        // Evaluate physical convective flux, physical dissipative flux, and source term
        conv_num_flux_dot_n[iquad] = conv_num_flux->evaluate_flux(soln_int[iquad], soln_ext[iquad], normal_int);

        conv_phys_flux_int[iquad] = pde_physics->convective_flux (soln_int[iquad]);
        conv_phys_flux_ext[iquad] = pde_physics->convective_flux (soln_ext[iquad]);

        diss_soln_num_flux[iquad] = diss_num_flux->evaluate_solution_flux(soln_int[iquad], soln_ext[iquad], normal_int);

        ADArrayTensor1 diss_soln_jump_int, diss_soln_jump_ext;
        for (int s=0; s<nstate; s++) {
            diss_soln_jump_int[s] = (diss_soln_num_flux[iquad][s] - soln_int[iquad][s]) * normal_int;
            diss_soln_jump_ext[s] = (diss_soln_num_flux[iquad][s] - soln_ext[iquad][s]) * normal_ext;
        }
        diss_flux_jump_int[iquad] = pde_physics->dissipative_flux (soln_int[iquad], diss_soln_jump_int);
        diss_flux_jump_ext[iquad] = pde_physics->dissipative_flux (soln_ext[iquad], diss_soln_jump_ext);

        diss_auxi_num_flux_dot_n[iquad] = diss_num_flux->evaluate_auxiliary_flux(
            soln_int[iquad], soln_ext[iquad],
            soln_grad_int[iquad], soln_grad_ext[iquad],
            normal_int, penalty);
    }

    // From test functions associated with interior cell point of view
    for (unsigned int itest_int=0; itest_int<n_dofs_int; ++itest_int) {
        ADtype rhs = 0.0;
        const unsigned int istate = fe_values_int.get_fe().system_to_component_index(itest_int).first;

        for (unsigned int iquad=0; iquad<n_face_quad_pts; ++iquad) {
            // Convection
            const ADtype flux_diff = conv_num_flux_dot_n[iquad][istate] - conv_phys_flux_int[iquad][istate]*normals_int[iquad];
            rhs = rhs - fe_values_int.shape_value_component(itest_int,iquad,istate) * flux_diff * JxW_int[iquad];
            // Diffusive
            rhs = rhs - fe_values_int.shape_value_component(itest_int,iquad,istate) * diss_auxi_num_flux_dot_n[iquad][istate] * JxW_int[iquad];
            rhs = rhs + fe_values_int.shape_grad_component(itest_int,iquad,istate) * diss_flux_jump_int[iquad][istate] * JxW_int[iquad];
        }

        local_rhs_int_cell(itest_int) += rhs.val();
        if (this->all_parameters->ode_solver_param.ode_solver_type == Parameters::ODESolverParam::ODESolverEnum::implicit_solver) {
            for (unsigned int idof = 0; idof < n_dofs_int; ++idof) {
                dR1_dW1[idof] = rhs.dx(idof);
            }
            for (unsigned int idof = 0; idof < n_dofs_ext; ++idof) {
                dR1_dW2[idof] = rhs.dx(n_dofs_int+idof);
            }
            this->system_matrix.add(dof_indices_int[itest_int], dof_indices_int, dR1_dW1);
            this->system_matrix.add(dof_indices_int[itest_int], dof_indices_ext, dR1_dW2);
        }
    }

    // From test functions associated with neighbour cell point of view
    for (unsigned int itest_ext=0; itest_ext<n_dofs_ext; ++itest_ext) {
        ADtype rhs = 0.0;
        const unsigned int istate = fe_values_int.get_fe().system_to_component_index(itest_ext).first;

        for (unsigned int iquad=0; iquad<n_face_quad_pts; ++iquad) {
            // Convection
            const ADtype flux_diff = (-conv_num_flux_dot_n[iquad][istate]) - conv_phys_flux_ext[iquad][istate]*(-normals_int[iquad]);
            rhs = rhs - fe_values_ext.shape_value_component(itest_ext,iquad,istate) * flux_diff * JxW_int[iquad];
            // Diffusive
            rhs = rhs - fe_values_ext.shape_value_component(itest_ext,iquad,istate) * (-diss_auxi_num_flux_dot_n[iquad][istate]) * JxW_int[iquad];
            rhs = rhs + fe_values_ext.shape_grad_component(itest_ext,iquad,istate) * diss_flux_jump_ext[iquad][istate] * JxW_int[iquad];
        }

        local_rhs_ext_cell(itest_ext) += rhs.val();
        if (this->all_parameters->ode_solver_param.ode_solver_type == Parameters::ODESolverParam::ODESolverEnum::implicit_solver) {
            for (unsigned int idof = 0; idof < n_dofs_int; ++idof) {
                dR2_dW1[idof] = rhs.dx(idof);
            }
            for (unsigned int idof = 0; idof < n_dofs_ext; ++idof) {
                dR2_dW2[idof] = rhs.dx(n_dofs_int+idof);
            }
            this->system_matrix.add(dof_indices_ext[itest_ext], dof_indices_int, dR2_dW1);
            this->system_matrix.add(dof_indices_ext[itest_ext], dof_indices_ext, dR2_dW2);
        }
    }
}
template class DGStrong <PHILIP_DIM, 1, double>;
template class DGStrong <PHILIP_DIM, 2, double>;
template class DGStrong <PHILIP_DIM, 3, double>;
template class DGStrong <PHILIP_DIM, 4, double>;
template class DGStrong <PHILIP_DIM, 5, double>;

} // PHiLiP namespace

