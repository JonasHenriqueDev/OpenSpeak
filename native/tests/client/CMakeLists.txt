qt_add_executable(tst_engine tst_engine.cpp)
target_link_libraries(tst_engine PRIVATE openspeak_engine Qt6::Test)
add_test(NAME tst_engine COMMAND tst_engine)

qt_add_executable(tst_cloudconnection tst_cloudconnection.cpp)
target_link_libraries(tst_cloudconnection PRIVATE openspeak_engine Qt6::Test)
add_test(NAME tst_cloudconnection COMMAND tst_cloudconnection)
set_tests_properties(tst_cloudconnection PROPERTIES TIMEOUT 15)

# Ponta a ponta: servidor + transmissor + espectador nativos no mesmo processo.
qt_add_executable(tst_p2p_e2e tst_p2p_e2e.cpp)
target_link_libraries(tst_p2p_e2e PRIVATE openspeak_engine openspeak_server_core Qt6::Test)
add_test(NAME tst_p2p_e2e COMMAND tst_p2p_e2e)
set_tests_properties(tst_p2p_e2e PROPERTIES TIMEOUT 120)

# Malha descentralizada (sem servidor): dois nós em loopback.
qt_add_executable(tst_mesh_e2e tst_mesh_e2e.cpp)
target_link_libraries(tst_mesh_e2e PRIVATE openspeak_engine Qt6::Test)
add_test(NAME tst_mesh_e2e COMMAND tst_mesh_e2e)
set_tests_properties(tst_mesh_e2e PROPERTIES TIMEOUT 120)
