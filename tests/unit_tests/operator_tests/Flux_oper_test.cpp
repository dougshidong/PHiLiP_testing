#include <iomanip>
#include <cmath>
#include <limits>
#include <type_traits>
#include <math.h>
#include <iostream>
#include <stdlib.h>

#include <ctime>

#include <deal.II/distributed/solution_transfer.h>

#include "testing/tests.h"

#include<fstream>
#include <deal.II/base/parameter_handler.h>
#include <deal.II/base/tensor.h>
#include <deal.II/numerics/vector_tools.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_refinement.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/grid_out.h>
#include <deal.II/grid/grid_in.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/dofs/dof_renumbering.h>

#include <deal.II/dofs/dof_accessor.h>

#include <deal.II/lac/vector.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/sparse_matrix.h>

#include <deal.II/meshworker/dof_info.h>

// Finally, we take our exact solution from the library as well as volume_quadrature
// and additional tools.
#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/data_out_dof_data.h>
#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/vector_tools.templates.h>

#include <deal.II/grid/manifold_lib.h>
#include <deal.II/fe/mapping_q.h>
#include <deal.II/fe/mapping_q_generic.h>
#include <deal.II/fe/fe_dgq.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/fe/mapping_fe_field.h> 

#include <deal.II/fe/fe_bernstein.h>

#include "parameters/all_parameters.h"
#include "parameters/parameters.h"
#include "operators/operators.h"
#include "dg/dg.h"
#include "dg/dg_factory.hpp"

const double TOLERANCE = 1E-6;
using namespace std;



int main (int argc, char * argv[])
{

    dealii::Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, 1);
    using real = double;
    using namespace PHiLiP;
    std::cout << std::setprecision(std::numeric_limits<long double>::digits10 + 1) << std::scientific;
    const int dim = PHILIP_DIM;
    const int nstate = 2;
    dealii::ParameterHandler parameter_handler;
    PHiLiP::Parameters::AllParameters::declare_parameters (parameter_handler);

    PHiLiP::Parameters::AllParameters all_parameters_new;
    all_parameters_new.parse_parameters (parameter_handler);
    all_parameters_new.nstate = nstate;
    dealii::ConditionalOStream pcout(std::cout, dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD)==0);

    using FR_enum = Parameters::AllParameters::Flux_Reconstruction;
    all_parameters_new.flux_reconstruction_type = FR_enum::cHU;
   // all_parameters_new.use_collocated_nodes=true;
    all_parameters_new.overintegration = 2;

    double left = 0.0;
    double right = 1.0;
    const bool colorize = true;
    const unsigned int igrid= 2;



    //Generate a standard grid

#if PHILIP_DIM==1 // dealii::parallel::distributed::Triangulation<dim> does not work for 1D
    using Triangulation = dealii::Triangulation<dim>;
    std::shared_ptr<Triangulation> grid = std::make_shared<Triangulation>(
        typename dealii::Triangulation<dim>::MeshSmoothing(
            dealii::Triangulation<dim>::smoothing_on_refinement |
            dealii::Triangulation<dim>::smoothing_on_coarsening));
#else
    using Triangulation = dealii::parallel::distributed::Triangulation<dim>;
    std::shared_ptr<Triangulation> grid = std::make_shared<Triangulation>(
        MPI_COMM_WORLD,
        typename dealii::Triangulation<dim>::MeshSmoothing(
            dealii::Triangulation<dim>::smoothing_on_refinement |
            dealii::Triangulation<dim>::smoothing_on_coarsening));
#endif
        dealii::GridGenerator::hyper_cube (*grid, left, right, colorize);
        grid->refine_global(igrid);
    double max_dif_int_parts = 0.0;
    for(unsigned int poly_degree=2; poly_degree<6; poly_degree++){


        OPERATOR::OperatorBase<dim,nstate,real> operators(&all_parameters_new, poly_degree, poly_degree, poly_degree); 

        const unsigned int n_dofs = operators.fe_collection_basis[poly_degree].dofs_per_cell;
        const unsigned int n_dofs_flux = operators.fe_collection_flux_basis[poly_degree].dofs_per_cell;
        const unsigned int n_quad_pts = operators.volume_quadrature_collection[poly_degree].size();
        std::vector<dealii::FullMatrix<real>> vol_int_parts(dim);
        std::vector<dealii::FullMatrix<real>> face_int_parts(dim);
        for(int idim=0; idim<dim; idim++){
            vol_int_parts[idim].reinit(n_dofs, nstate * n_dofs_flux);
            face_int_parts[idim].reinit(n_dofs, nstate * n_dofs_flux);
        //    vol_int_parts[idim].add(1.0, operators.local_flux_basis_stiffness[poly_degree][idim]);
            //have to do weak flux basis vol integral
//    pcout<<"VOL "<<std::endl;
            for(unsigned int itest=0; itest<n_dofs; itest++){
                const unsigned int istate_test = operators.fe_collection_basis[poly_degree].system_to_component_index(itest).first;
                const unsigned int ishape_test = operators.fe_collection_basis[poly_degree].system_to_component_index(itest).second;
                for(unsigned int idof=0; idof<n_dofs_flux; idof++){
                    for(unsigned int istate_dof=0; istate_dof<nstate; istate_dof++){
                       // const unsigned int istate_dof = operators.fe_collection_basis[poly_degree].system_to_component_index(idof).first;
                       // const unsigned int ishape_dof = operators.fe_collection_basis[poly_degree].system_to_component_index(idof).second;
                        double value= 0.0;
                        for(unsigned int iquad=0; iquad<n_quad_pts; iquad++){
                            value +=        operators.vol_integral_gradient_basis[poly_degree][istate_test][idim][iquad][ishape_test] 
                                    *       operators.flux_basis_at_vol_cubature[poly_degree][istate_dof][iquad][idof];
                        }
                        if(istate_test == istate_dof){
                            unsigned int dof_index = idof + n_dofs_flux * istate_dof;
                            vol_int_parts[idim][itest][dof_index] += value;
                            vol_int_parts[idim][itest][dof_index] += operators.local_flux_basis_stiffness[poly_degree][istate_dof][idim][ishape_test][idof];
                        }
                    }
                }
            }
//            for(unsigned int itest=0; itest<n_dofs; itest++){
//                for(unsigned int idof=0; idof<nstate * n_dofs_flux; idof++){
//pcout<<vol_int_parts[idim][itest][idof]<<" ";
//}
//pcout<<std::endl;
//}
          //  vol_int_parts[idim].Tadd(1.0, operators.local_basis_stiffness[poly_degree][idim]);
        }
        const unsigned int n_quad_face_pts = operators.face_quadrature_collection[poly_degree].size();
        for(unsigned int iface=0; iface< dealii::GeometryInfo<dim>::faces_per_cell; iface++){
            const dealii::Tensor<1,dim,real> unit_normal = dealii::GeometryInfo<dim>::unit_normal_vector[iface];
            int jdim=0;
            for(int idim=0; idim<dim; idim++){
                if(unit_normal[idim] != 0)
                    jdim = idim;
            }
//    pcout<<"face "<<std::endl;
            for(unsigned int itest=0; itest<n_dofs; itest++){
                const unsigned int istate_test = operators.fe_collection_basis[poly_degree].system_to_component_index(itest).first;
                for(unsigned int idof=0; idof<n_dofs_flux; idof++){
                    for(unsigned int istate_dof=0; istate_dof<nstate; istate_dof++){
                       // const unsigned int istate_dof = operators.fe_collection_basis[poly_degree].system_to_component_index(idof).first;
                       // const unsigned int ishape_dof = operators.fe_collection_basis[poly_degree].system_to_component_index(idof).second;
                        double value= 0.0;
                        for(unsigned int iquad=0; iquad<n_quad_face_pts; iquad++){
                            value +=        operators.face_integral_basis[poly_degree][iface][jdim][iquad][itest] 
                                    *       operators.flux_basis_at_facet_cubature[poly_degree][istate_dof][iface][iquad][idof];
                        }
                        if(istate_test == istate_dof){
                            unsigned int dof_index = idof + n_dofs_flux * istate_dof;
                            face_int_parts[jdim][itest][dof_index] += value;
                        }
                    }
                }
            }
//            for(unsigned int itest=0; itest<n_dofs; itest++){
//                for(unsigned int idof=0; idof<nstate * n_dofs_flux; idof++){
//pcout<<face_int_parts[jdim][itest][idof]<<" ";
//}
//pcout<<std::endl;
//}
        }

        for(int idim=0; idim<dim; idim++){
            for(unsigned int idof=0; idof<n_dofs; idof++){
                for(unsigned int idof2=0; idof2<(nstate * n_dofs_flux); idof2++){
                    if(std::abs(face_int_parts[idim][idof][idof2] - vol_int_parts[idim][idof][idof2])>max_dif_int_parts)
                        max_dif_int_parts = std::abs(face_int_parts[idim][idof][idof2] - vol_int_parts[idim][idof][idof2]);
                }
            }
        }
        
    }//end of poly_degree loop

    const double max_dif_int_parts_mpi= (dealii::Utilities::MPI::max(max_dif_int_parts, MPI_COMM_WORLD));
pcout<<"max dif "<<max_dif_int_parts_mpi<<std::endl;
    if( max_dif_int_parts_mpi >1e-7){
        pcout<<" Surface operator not satisfy integration by parts !"<<std::endl;
       // printf(" One of the pth order deirvatives is wrong !\n");
        return 1;
    }
    else{
        return 0;
    }

}//end of main
