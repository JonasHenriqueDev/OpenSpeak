qt_add_executable(tst_engine tst_engine.cpp)
qt_add_executable(tst_updates tst_updates.cpp)
target_link_libraries(tst_updates PRIVATE openspeak_engine Qt6::Test)
add_test(NAME tst_updates COMMAND tst_updates)
target_link_libraries(tst_engine PRIVATE openspeak_engine Qt6::Test)
add_test(NAME tst_engine COMMAND tst_engine)

qt_add_executable(tst_cloudconnection tst_cloudconnection.cpp)
target_link_libraries(tst_cloudconnection PRIVATE openspeak_engine Qt6::Test)
add_test(NAME tst_cloudconnection COMMAND tst_cloudconnection)
set_tests_properties(tst_cloudconnection PROPERTIES TIMEOUT 15)

# Ponta a ponta: servidor + transmissor + espectador nativos no mesmo processo.
qt_add_executable(tst_p2p_e2e tst_p2p_e2e.cpp)
target_link_libraries(tst_p2p_e2e PRIVATE openspeak_engine openspeak_server_host Qt6::Test)
add_test(NAME tst_p2p_e2e COMMAND tst_p2p_e2e)
set_tests_properties(tst_p2p_e2e PROPERTIES TIMEOUT 120)

# Ponta a ponta no modo SFU: servidor com relay real + 1 transmissor + 2 espectadores.
if(OPENSPEAK_BUILD_SFU)
    qt_add_executable(tst_sfu_e2e tst_sfu_e2e.cpp)
    target_link_libraries(tst_sfu_e2e PRIVATE openspeak_engine openspeak_server_host Qt6::Test)
    add_test(NAME tst_sfu_e2e COMMAND tst_sfu_e2e)
    set_tests_properties(tst_sfu_e2e PROPERTIES TIMEOUT 120)
endif()

# Malha descentralizada (sem servidor): dois nós em loopback.
qt_add_executable(tst_mesh_e2e tst_mesh_e2e.cpp)
target_link_libraries(tst_mesh_e2e PRIVATE openspeak_engine Qt6::Test)
add_test(NAME tst_mesh_e2e COMMAND tst_mesh_e2e)
set_tests_properties(tst_mesh_e2e PROPERTIES TIMEOUT 120)

# Captura de janela sem borda (duplicação da tela + PrintWindow) contra janelas reais.
qt_add_executable(tst_windowgrabber tst_windowgrabber.cpp)
target_link_libraries(tst_windowgrabber PRIVATE openspeak_engine Qt6::Gui Qt6::Test)
add_test(NAME tst_windowgrabber COMMAND tst_windowgrabber)
set_tests_properties(tst_windowgrabber PROPERTIES TIMEOUT 30)
