cmake_minimum_required(VERSION 3.25)
project(fc5mp_client CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG 4a455528f61b5a375b1f9d44e7d296d47f18bb18)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)  # MinHook 1.3.3 ships a pre-3.5 CMakeLists
  FetchContent_MakeAvailable(minhook)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_library(version SHARED src/version_proxy/dllmain.cpp)
  set_target_properties(version PROPERTIES PREFIX "" OUTPUT_NAME "version"
    LINK_FLAGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version_proxy/version_proxy.def")

  # hook core links MinHook (proves the fetch + cross-compile works)
  add_library(fc5mp_hookcore STATIC src/hookcore/hookcore.cpp)
  target_link_libraries(fc5mp_hookcore PRIVATE minhook)
  target_include_directories(fc5mp_hookcore PRIVATE ${minhook_SOURCE_DIR}/include)

  add_library(fc5mp-client SHARED
    src/client/dllmain.cpp
    src/client/versionlock.cpp
    src/client/sha256.cpp
    src/client/targets.cpp
    src/sig/signature.cpp
    src/capture/record.cpp
    src/capture/udp_frame.cpp
    src/client/sink.cpp
    src/client/net_hooks.cpp
    src/client/redirect.cpp
    src/client/overlapped.cpp
    src/client/tls_hooks.cpp
    src/client/uplay_hooks.cpp
    src/client/memdump.cpp
    src/capture/redact.cpp
    src/hookcore/hookcore.cpp)
  set_target_properties(fc5mp-client PROPERTIES PREFIX "" OUTPUT_NAME "fc5mp-client")
  target_include_directories(fc5mp-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${minhook_SOURCE_DIR}/include)
  target_link_libraries(fc5mp-client PRIVATE minhook ws2_32)

  # Windows integration test for the ws2_32 detours; run under Wine:
  #   wine build-win/test_net_hooks.exe
  add_executable(test_net_hooks tests/win/test_net_hooks.cpp
    src/client/net_hooks.cpp src/client/redirect.cpp src/client/overlapped.cpp
    src/client/sink.cpp
    src/capture/record.cpp src/capture/udp_frame.cpp src/hookcore/hookcore.cpp)
  target_include_directories(test_net_hooks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${minhook_SOURCE_DIR}/include)
  target_link_libraries(test_net_hooks PRIVATE minhook ws2_32)

  add_executable(test_overlapped_hooks tests/win/test_overlapped_hooks.cpp
    src/client/net_hooks.cpp src/client/redirect.cpp src/client/overlapped.cpp
    src/client/sink.cpp
    src/capture/record.cpp src/capture/udp_frame.cpp src/hookcore/hookcore.cpp)
  target_include_directories(test_overlapped_hooks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${minhook_SOURCE_DIR}/include)
  target_link_libraries(test_overlapped_hooks PRIVATE minhook ws2_32)

  # Windows integration test for the TLS (SSL_read/SSL_write) detours; run under
  # Wine:  wine build-win/test_tls_hooks.exe
  add_executable(test_tls_hooks tests/win/test_tls_hooks.cpp
    src/client/tls_hooks.cpp src/client/sink.cpp src/client/redirect.cpp
    src/capture/record.cpp src/capture/redact.cpp src/hookcore/hookcore.cpp)
  target_include_directories(test_tls_hooks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${minhook_SOURCE_DIR}/include)
  target_link_libraries(test_tls_hooks PRIVATE minhook ws2_32)

  add_library(uplay_r1_loader64 SHARED src/uplay_stub/uplay_stub.cpp)
  set_target_properties(uplay_r1_loader64 PROPERTIES PREFIX "" OUTPUT_NAME "uplay_r1_loader64"
    LINK_FLAGS "${CMAKE_CURRENT_SOURCE_DIR}/src/uplay_stub/uplay_r1_loader64.def")
  target_link_libraries(uplay_r1_loader64 PRIVATE shell32)

  add_library(uplay_aux_r164 SHARED src/uplay_stub/uplay_aux.cpp)
  set_target_properties(uplay_aux_r164 PROPERTIES PREFIX "" OUTPUT_NAME "uplay_aux_r164"
    LINK_FLAGS "${CMAKE_CURRENT_SOURCE_DIR}/src/uplay_stub/uplay_aux_r164.def")

  add_executable(UbisoftGameLauncher WIN32 src/uplay_stub/game_launcher_stub.cpp)
  set_target_properties(UbisoftGameLauncher PROPERTIES OUTPUT_NAME "UbisoftGameLauncher")

  add_library(dbdata SHARED src/uplay_stub/dbdata_stub.cpp)
  set_target_properties(dbdata PROPERTIES PREFIX "" OUTPUT_NAME "dbdata"
    LINK_FLAGS "${CMAKE_CURRENT_SOURCE_DIR}/src/uplay_stub/dbdata.def")
else()
  enable_testing()
  add_executable(test_smoke tests/host/test_smoke.cpp)
  add_test(NAME smoke COMMAND test_smoke)
  add_executable(test_exports tests/host/test_exports.cpp src/pe/exports.cpp)
  add_executable(test_hookcore_iface tests/host/test_hookcore_iface.cpp src/hookcore/hookcore.cpp)
  add_test(NAME hookcore COMMAND test_hookcore_iface)
  add_executable(test_record tests/host/test_record.cpp src/capture/record.cpp)
  add_test(NAME record COMMAND test_record)
  add_executable(test_udp_frame tests/host/test_udp_frame.cpp src/capture/udp_frame.cpp)
  add_test(NAME udp_frame COMMAND test_udp_frame)
  add_executable(test_sink tests/host/test_sink.cpp src/client/sink.cpp src/capture/record.cpp)
  target_include_directories(test_sink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME sink COMMAND test_sink)
  add_executable(test_redact tests/host/test_redact.cpp src/capture/redact.cpp)
  target_compile_options(test_redact PRIVATE -fsanitize=address,undefined)
  target_link_options(test_redact PRIVATE -fsanitize=address,undefined)
  add_test(NAME redact COMMAND test_redact)
  add_executable(test_redirect tests/host/test_redirect.cpp src/client/redirect.cpp)
  target_include_directories(test_redirect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME redirect COMMAND test_redirect)
  add_executable(test_overlapped tests/host/test_overlapped.cpp src/client/overlapped.cpp)
  target_include_directories(test_overlapped PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME overlapped COMMAND test_overlapped)
  add_executable(test_versionlock tests/host/test_versionlock.cpp src/client/versionlock.cpp)
  add_test(NAME versionlock COMMAND test_versionlock)
  add_executable(test_sha256 tests/host/test_sha256.cpp src/client/sha256.cpp)
  add_test(NAME sha256 COMMAND test_sha256)
  set_tests_properties(sha256 PROPERTIES ENVIRONMENT
    "FC5_DLL=/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin/FC_m64.dll")
  add_executable(test_targets tests/host/test_targets.cpp src/client/targets.cpp src/sig/signature.cpp)
  target_include_directories(test_targets PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME targets COMMAND test_targets)
  set_tests_properties(targets PROPERTIES ENVIRONMENT
    "FC5_DLL=/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin/FC_m64.dll")
  add_executable(test_signature tests/host/test_signature.cpp src/sig/signature.cpp)
  add_test(NAME signature COMMAND test_signature)
  set_tests_properties(signature PROPERTIES ENVIRONMENT
    "FC5_DLL=/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin/FC_m64.dll")
  add_test(NAME exports COMMAND test_exports "/home/kalopsia/.local/share/Steam/steamapps/common/Proton 10.0/files/lib/wine/x86_64-windows/version.dll")
endif()
