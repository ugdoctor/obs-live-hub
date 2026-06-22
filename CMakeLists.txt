cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Core Network)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/auth/GoogleOAuth.cpp
    src/auth/GoogleOAuth.hpp
    src/auth/TwitchOAuth.cpp
    src/auth/TwitchOAuth.hpp
    src/core/PluginConfig.cpp
    src/core/PluginConfig.hpp
    src/platforms/YouTubePlatform.cpp
    src/platforms/YouTubePlatform.hpp
    src/platforms/TlsSocket.cpp
    src/platforms/TlsSocket.hpp
    src/platforms/TwitchPlatform.cpp
    src/platforms/TwitchPlatform.hpp
    src/ui/CommentDock.cpp
    src/ui/CommentDock.hpp
    src/ui/OverlayStyleDialog.cpp
    src/ui/OverlayStyleDialog.hpp
    src/ui/OverlayUtils.cpp
    src/ui/OverlayUtils.hpp
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.hpp
    src/ui/StreamSettingsDialog.cpp
    src/ui/StreamSettingsDialog.hpp
    src/ui/VoteManagerDialog.cpp
    src/ui/VoteManagerDialog.hpp
    src/ui/TtsDictionaryDialog.cpp
    src/ui/TtsDictionaryDialog.hpp
    src/ui/TtsSpeechDialog.cpp
    src/ui/TtsSpeechDialog.hpp
    src/ui/DebugSettingsDialog.cpp
    src/ui/DebugSettingsDialog.hpp
    src/ui/AivisParamLimitDialog.cpp
    src/ui/AivisParamLimitDialog.hpp
    src/modules/WsServer.cpp
    src/modules/WsServer.hpp
    src/modules/AivisEngine.cpp
    src/modules/AivisEngine.hpp
    src/modules/AivisStyleCache.cpp
    src/modules/AivisStyleCache.hpp
    src/modules/EffectManager.cpp
    src/modules/EffectManager.hpp
    src/ui/EffectSettingsDialog.cpp
    src/ui/EffectSettingsDialog.hpp
    src/modules/PointManager.cpp
    src/modules/PointManager.hpp
    src/ui/PointSettingsDialog.cpp
    src/ui/PointSettingsDialog.hpp
    src/modules/BouyomiChanClient.cpp
    src/modules/BouyomiChanClient.hpp
    src/modules/VoiceroidClient.cpp
    src/modules/VoiceroidClient.hpp
    src/modules/EngineManager.cpp
    src/modules/EngineManager.hpp
    src/ui/BouyomiParamLimitDialog.cpp
    src/ui/BouyomiParamLimitDialog.hpp
    src/ui/BrowserDiagDialog.cpp
    src/ui/BrowserDiagDialog.hpp
    src/ui/ConversationOverlayDialog.cpp
    src/ui/ConversationOverlayDialog.hpp
    src/modules/ViewerTtsSettings.cpp
    src/modules/ViewerTtsSettings.hpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

# WinSock2 + SChannel (TlsSocket で使用)
if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 secur32 winhttp)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
