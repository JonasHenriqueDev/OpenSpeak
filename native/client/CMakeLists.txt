
# ---------------------------------------------------------------- motor
# Biblioteca separada da UI para os testes poderem usá-la.
add_library(openspeak_engine STATIC
    core/ServerConnection.cpp core/ServerConnection.h
    core/ConnectionHandler.cpp core/ConnectionHandler.h
    core/Settings.cpp core/Settings.h
    mesh/PeerDirectory.cpp mesh/PeerDirectory.h
    mesh/MeshConnection.cpp mesh/MeshConnection.h
    media/VideoSource.cpp media/VideoSource.h
    media/VideoEncoder.cpp media/VideoEncoder.h
    media/VideoDecoder.cpp media/VideoDecoder.h
    media/AudioCodec.cpp media/AudioCodec.h
    media/AudioCapture.cpp media/AudioCapture.h
    media/AudioPlayer.cpp media/AudioPlayer.h
    rtc/RtpStatsHandler.cpp rtc/RtpStatsHandler.h
    rtc/SdpFilter.cpp rtc/SdpFilter.h
    rtc/RtcSession.cpp rtc/RtcSession.h
    rtc/SessionOutbound.cpp rtc/SessionOutbound.h
    rtc/SessionInbound.cpp rtc/SessionInbound.h
    video/Presets.h
    video/StreamStats.cpp video/StreamStats.h
    video/Layout.cpp video/Layout.h
    video/ScreenshareManager.cpp video/ScreenshareManager.h
)
target_include_directories(openspeak_engine PUBLIC core media mesh rtc video)
if(TARGET LibDataChannel::LibDataChannelStatic)
    set(OPENSPEAK_LDC LibDataChannel::LibDataChannelStatic)
else()
    set(OPENSPEAK_LDC LibDataChannel::LibDataChannel)
endif()
target_link_libraries(openspeak_engine PUBLIC
    openspeak_common
    Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::WebSockets
    ${OPENSPEAK_LDC}
    openspeak_ffmpeg
    ole32 mmdevapi
)
target_compile_definitions(openspeak_engine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

# ---------------------------------------------------------------- app
qt_add_executable(OpenSpeak WIN32 main.cpp)
set_target_properties(OpenSpeak PROPERTIES OUTPUT_NAME "OpenSpeak")

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(OpenSpeak
    URI OpenSpeak
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/LoginView.qml
        qml/MainView.qml
        qml/ChannelTree.qml
        qml/StreamArea.qml
        qml/StreamTile.qml
        qml/SetupStreamDialog.qml
        qml/ViewersPanel.qml
        qml/StatsPanel.qml
        qml/StatGraph.qml
        qml/Toasts.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/I18n.js
)

target_link_libraries(OpenSpeak PRIVATE openspeak_engine Qt6::Quick Qt6::QuickControls2)

# Copia as DLLs do Qt ao lado do .exe (executável autônomo).
find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT)
    add_custom_command(TARGET OpenSpeak POST_BUILD
        COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:OpenSpeak>"
        COMMENT "windeployqt OpenSpeak")
endif()
