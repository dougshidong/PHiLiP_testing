set(TEST_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
add_subdirectory(snapshot_generation)

# ----------------------------------------
# - description
# ----------------------------------------
configure_file(1d_burgers_rewienski_reduced_order.prm 1d_burgers_rewienski_reduced_order.prm COPYONLY)
configure_file(3_0.02_1d_burgers_rewienski_snapshot.prm 3_0.02_1d_burgers_rewienski_snapshot.prm COPYONLY)
configure_file(6_0.05_1d_burgers_rewienski_snapshot.prm 6_0.05_1d_burgers_rewienski_snapshot.prm COPYONLY)
configure_file(9_0.075_1d_burgers_rewienski_snapshot.prm 9_0.075_1d_burgers_rewienski_snapshot.prm COPYONLY)
add_test(
        NAME 1D_BURGERS_REWIENSKI_REDUCED_ORDER
        COMMAND bash -c
        "mpirun -np 1 ${EXECUTABLE_OUTPUT_PATH}/PHiLiP_1D -i ${CMAKE_CURRENT_BINARY_DIR}/3_0.02_1d_burgers_rewienski_snapshot.prm ;/
        mpirun -np 1 ${EXECUTABLE_OUTPUT_PATH}/PHiLiP_1D -i ${CMAKE_CURRENT_BINARY_DIR}/6_0.05_1d_burgers_rewienski_snapshot.prm ;/
        mpirun -np 1 ${EXECUTABLE_OUTPUT_PATH}/PHiLiP_1D -i ${CMAKE_CURRENT_BINARY_DIR}/9_0.075_1d_burgers_rewienski_snapshot.prm ;/
        mpirun -np 1 ${EXECUTABLE_OUTPUT_PATH}/PHiLiP_1D -i ${CMAKE_CURRENT_BINARY_DIR}/1d_burgers_rewienski_reduced_order.prm"
        WORKING_DIRECTORY ${TEST_OUTPUT_DIR}
)

# =======================================
# Default manufactured solution
# =======================================
configure_file(1d_burgers_rewienski_manufactured.prm 1d_burgers_rewienski_manufactured.prm COPYONLY)
add_test(
        NAME 1D_BURGERS_REWIENSKI_MANUFACTURED_SOLUTION
        COMMAND mpirun -np 1 ${EXECUTABLE_OUTPUT_PATH}/PHiLiP_1D -i ${CMAKE_CURRENT_BINARY_DIR}/1d_burgers_rewienski_manufactured.prm
        WORKING_DIRECTORY ${TEST_OUTPUT_DIR}
)
