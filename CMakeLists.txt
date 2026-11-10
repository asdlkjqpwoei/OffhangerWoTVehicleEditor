cmake_minimum_required(VERSION 4.2)
# Keep existing build trees and newly generated solutions on the same configurations.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported build configurations" FORCE)
project(OffHangerWoTOfflineVehicleEditor LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:Embedded>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_SUPPRESS_REGENERATION ON)
option(OHEDITOR_BUILD_APP "Build the WinUI editor" ON)
option(OHEDITOR_BUILD_TESTS "Build the native verification program" ON)
set(CoreSources
	core/Archive.cpp core/Catalog.cpp core/Common.cpp core/Configuration.cpp
	core/Crew.cpp core/DataSection.cpp core/Defaults.cpp core/GameDataCache.cpp core/Geometry.cpp core/Havok.cpp
	core/ResourceSignature.cpp core/Json.cpp core/ModelCache.cpp core/Pickle.cpp core/SaveGame.cpp core/SaveSession.cpp
	core/VehicleFactory.cpp core/VehicleWorkspace.cpp core/VehicleParameters.cpp
)
file(GLOB CoreHeaders CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/core/*.h")
file(GLOB YamlSources "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yaml-cpp/src/*.cpp")
set(Dependencies third_party/pugixml/pugixml.cpp third_party/miniz/miniz_tinfl.c ${YamlSources})
function(ConfigureCore Target)
	target_sources(${Target} PRIVATE ${CoreSources} ${CoreHeaders} ${Dependencies})
	target_include_directories(${Target} PRIVATE core third_party/pugixml third_party/miniz
		third_party/yaml-cpp/include third_party/yaml-cpp/src)
	target_compile_definitions(${Target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS YAML_CPP_STATIC_DEFINE)
	target_compile_options(${Target} PRIVATE /utf-8 /bigobj)
	target_link_libraries(${Target} PRIVATE bcrypt)
	set_target_properties(${Target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/core" PREFIX "Core" FILES ${CoreSources} ${CoreHeaders})
	source_group("Third party" FILES ${Dependencies})
endfunction()
if(OHEDITOR_BUILD_APP)
	if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
		message(FATAL_ERROR "Use the vs2026 preset to build the WinUI application.")
	endif()
	set(AppSources app/Main.cpp app/EditorWindow.cpp app/BaselinePage.cpp app/VehiclePage.cpp app/ParameterPage.cpp
		app/XmlPage.cpp app/ModelPage.cpp app/ModelViewport.cpp app/SavePage.cpp app/UiCommon.cpp app/EditBuffer.cpp app/SessionLoading.cpp app/WorkspaceOperations.cpp)
	add_executable(OffHangerWoTOfflineVehicleEditor WIN32 ${AppSources} app/EditorWindow.h app/UiCommon.h
		app/ModelViewport.h app/app.manifest)
	ConfigureCore(OffHangerWoTOfflineVehicleEditor)
	target_link_libraries(OffHangerWoTOfflineVehicleEditor PRIVATE windowsapp user32 shell32 ole32 comdlg32 d3d11 dxgi d3dcompiler)
	target_compile_definitions(OffHangerWoTOfflineVehicleEditor PRIVATE UNICODE _UNICODE)
	set_target_properties(OffHangerWoTOfflineVehicleEditor PROPERTIES
		OUTPUT_NAME "OffHangerWoTOfflineVehicleEditor"
		VS_GLOBAL_RootNamespace OffHangerWoTOfflineVehicleEditor VS_GLOBAL_AppxPackage false
		VS_GLOBAL_WindowsPackageType None VS_GLOBAL_OutputType WinExe
		VS_GLOBAL_PrependPriInitialPath false VS_GLOBAL_ProjectPriIndexName Application
		VS_GLOBAL_ProjectPriFileName resources.pri VS_GLOBAL_AppContainerApplication false
		VS_GLOBAL_WindowsAppSDKSelfContained true VS_GLOBAL_WindowsAppSdkAutoInitialize false
		VS_GLOBAL_UseWinUI true VS_GLOBAL_CppWinRTOptimized true VS_GLOBAL_CppWinRTRootNamespaceAutoMerge true
		# C++/WinRT projections have long namespace filenames. Keep their
		# directory short even when users extract the project in a deep path.
		VS_GLOBAL_GeneratedFilesDir "$(ProjectDir)g\\$(Configuration)\\"
		VS_GLOBAL_WindowsTargetPlatformMinVersion "10.0.19041.0"
		VS_GLOBAL_NuGetAudit false VS_GLOBAL_RestoreRecursive false
		VS_USER_PROPS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WinUI.props"
		VS_PACKAGE_REFERENCES "Microsoft.Windows.CppWinRT_2.0.250303.1;Microsoft.WindowsAppSDK.WinUI_1.8.260224000;Microsoft.Web.WebView2_1.0.3179.45;Microsoft.Windows.SDK.BuildTools_10.0.26100.4654"
	)
	set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT OffHangerWoTOfflineVehicleEditor)
	set(EditorAssets Config/ui.yaml Config/parameters.yaml Config/vehicles.yaml)
	foreach(Asset IN LISTS EditorAssets)
		set(Source "${CMAKE_CURRENT_SOURCE_DIR}/assets/${Asset}")
		set(Output "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<CONFIG>/${Asset}")
		get_filename_component(AssetFolder "${Asset}" DIRECTORY)
		add_custom_command(OUTPUT "${Output}"
			MAIN_DEPENDENCY "${Source}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<CONFIG>/${AssetFolder}"
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${Source}" "${Output}"
			VERBATIM)
		# Attach the copy rule to the original asset, keeping output copies out of the IDE.
		target_sources(OffHangerWoTOfflineVehicleEditor PRIVATE "${Source}")
		source_group("Configuration and defaults" FILES "${Source}")
	endforeach()
endif()
if(OHEDITOR_BUILD_TESTS)
	add_executable(OffHangerWoTOfflineVehicleEditorTests tests/OffHangerWoTOfflineVehicleEditorTests.cpp)
	ConfigureCore(OffHangerWoTOfflineVehicleEditorTests)
	target_compile_definitions(OffHangerWoTOfflineVehicleEditorTests PRIVATE OHEDITOR_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
	enable_testing()
	add_test(NAME editor-core COMMAND OffHangerWoTOfflineVehicleEditorTests)
endif()

# Runtime dependencies are found through the active compiler, not a developer's installation path.
if(OHEDITOR_BUILD_APP)
	configure_file(cmake/InstallRuntime.cmake.in "${CMAKE_BINARY_DIR}/InstallRuntime.cmake" @ONLY)
	install(SCRIPT "${CMAKE_BINARY_DIR}/InstallRuntime.cmake")
	set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
	set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
	include(InstallRequiredSystemLibraries)
	install(FILES README.md DESTINATION .)
	install(DIRECTORY third_party/ DESTINATION Licenses FILES_MATCHING PATTERN "LICENSE*" PATTERN "*.MIT")
endif()
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "OffHangerWoTOfflineVehicleEditor")
set(CPACK_PACKAGE_VERSION "unversioned")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_SOURCE_GENERATOR ZIP)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-Source")
set(CPACK_SOURCE_IGNORE_FILES "/build/;/bin/;/.git/;/.vs/;/.agents/;/.codex/;/Cache/;/settings.yaml$;/script[.]pkg$;/defaults.yaml$;/Notice[.]md$;/WORKLOG.md$;/RedesignWork.md$;/assets/Defaults/scripts[.]pkg$;[.]user$;[.]ohcache$;[.]ohvehicles$;[.]dat$")
include(CPack)
