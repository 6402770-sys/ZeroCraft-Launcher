project(tests)
include(ECMMarkAsTest)
include(ECMMarkNonGuiExecutable)

set(TEST_SOURCES
    FileSystem_test.cpp
    GZip_test.cpp
    GradleSpecifier_test.cpp
    MojangVersionFormat_test.cpp
    Library_test.cpp
    ResourceFolderModel_test.cpp
    ResourcePackParse_test.cpp
    TexturePackParse_test.cpp
    DataPackParse_test.cpp
    ShaderPackParse_test.cpp
    WorldSaveParse_test.cpp
    ParseUtils_test.cpp
    Task_test.cpp
    INIFile_test.cpp
    JavaVersion_test.cpp
    Packwiz_test.cpp
    Index_test.cpp
    Version_test.cpp
    ModrinthUrl_test.cpp
    MetaComponentParse_test.cpp
    CatPack_test.cpp
    XmlLogs_test.cpp
    Commandline_test.cpp
    ZeroStyle_test.cpp
    ZeroTheme_test.cpp
    CustomTheme_test.cpp
    ThemeEditorModel_test.cpp
    ZeroThemeEditor_test.cpp
    ZeroThemeAssets_test.cpp
    ZeroPixelFont_test.cpp
    ZeroFormat_test.cpp
    ZeroMotion_test.cpp
    ZeroSmoothScroll_test.cpp
    ZeroSound_test.cpp
    ZeroPlayButton_test.cpp
    ZeroShell_test.cpp
    ZeroVideoBackground_test.cpp
    CrashDetective_test.cpp
    CommandMatcher_test.cpp
    ZeroCommandPalette_test.cpp
    InstanceProfile_test.cpp
    SessionHistory_test.cpp
    PlayTracker_test.cpp
    SessionSummary_test.cpp
    WhilePlaying_test.cpp
    HomeServers_test.cpp
    LibraryFilter_test.cpp
    ZeroLibrary_test.cpp
    ZeroExportChooser_test.cpp
    WorldScanner_test.cpp
    ScreenshotScanner_test.cpp
    WorldBackup_test.cpp
    PackSnapshot_test.cpp
    AutoBackup_test.cpp
    StorageScanner_test.cpp
    JavaDoctor_test.cpp
    ModHealth_test.cpp
    DependencyInspector_test.cpp
    ModRepair_test.cpp
    ModSnapshot_test.cpp
    ServerVault_test.cpp
    FavoriteStore_test.cpp
    TechnicMeta_test.cpp
    ContentMetadata_test.cpp
    ContentFilter_test.cpp
    CreateInstanceModel_test.cpp
    ZeroCreateInstance_test.cpp
    ZeroContentKit_test.cpp
    ZeroDiscover_test.cpp
    ZeroDiscoverMods_test.cpp
    ZeroDiscoverPacks_test.cpp
    ZeroDiscoverShaders_test.cpp
    ZeroDiscoverWorlds_test.cpp
    ZeroInstallCart_test.cpp
    ZeroModTarget_test.cpp
    ZeroModInstall_test.cpp
    ZeroModDependencies_test.cpp
    ZeroModpackInstall_test.cpp
    ModpackKeep_test.cpp
    ZeroModsPage_test.cpp
    PackOrder_test.cpp
    PackRollback_test.cpp
    ZeroResourcePacksPage_test.cpp
    ZeroShaderPacksPage_test.cpp
    JvmArgsAdvisor_test.cpp
    PreLaunchCheck_test.cpp
    PlaytimeStats_test.cpp
    PerformanceProfile_test.cpp
    ZeroElyAuth_test.cpp
    ZeroPerformanceDialog_test.cpp
    ZeroPreLaunchDialog_test.cpp
    ZeroPlaytimeView_test.cpp
    DropPlan_test.cpp
    ZeroDropAddDialog_test.cpp
    DownloadCenter_test.cpp
    ZeroDownloadsPanel_test.cpp
    UpdateSweep_test.cpp
    ZeroHomeUpdatesSection_test.cpp
    ZeroAddAccount_test.cpp
    ZeroIntro_test.cpp
    ZeroTour_test.cpp
)

# NOTE(@getchoo): Very funny way of aliasing the Qt generated `main` test functions to respective filenames
# This is required for any test file passed to `create_test_sourcelist()`
foreach(src ${TEST_SOURCES})
    cmake_path(GET src STEM funcName)
    set_source_files_properties(${src} PROPERTIES COMPILE_DEFINITIONS main=${funcName})
    list(APPEND TEST_NAMES ${funcName})
endforeach()

create_test_sourcelist(DRIVER_SOURCES LauncherTests.cpp ${TEST_SOURCES})
add_executable(LauncherTests ${DRIVER_SOURCES})
target_link_libraries(LauncherTests PRIVATE Launcher_logic Qt${QT_VERSION_MAJOR}::Test)

# Add back some stuff ECMAddTests would (https://api.kde.org/ecm/module/ECMAddTests.html)
ecm_mark_as_test(LauncherTests)
# NOTE(@getchoo): This is only for parity with our previous configurations, which all had no GUI
# In the future this might change....and I don't know how to handle it right now. Have fun!
ecm_mark_nongui_executable(LauncherTests)
# "The tests will be build with -DQT_FORCE_ASSERTS to enable assertions in the test executable even for release builds"
target_compile_definitions(LauncherTests PRIVATE QT_FORCE_ASSERTS)

foreach(test ${TEST_NAMES})
    string(REPLACE "_test" "" display_name ${test})
    add_test(NAME ${display_name} COMMAND LauncherTests ${test})
endforeach()

#[[
# TODO(@getchoo): Use this when we can use CMake 4.4 as our minimum version....one day
discover_tests(
    COMMAND LauncherTests
    DISCOVERY_ARGS -N
    DISCOVERY_MATCH "^(.+)$"
    TEST_NAME "\\1"
    TEST_ARGS "\\1"
)
]]#
