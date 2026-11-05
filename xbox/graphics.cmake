# Include after adding nxdk-gles11 with NXDK_GLES11_WITH_GL4ES=ON. Link the game
# target against GL and nxdk's libpbkit.lib. This fragment changes dependencies,
# not the application's platform sources.
if(NOT TARGET GL OR NOT TARGET GLESv1_CM)
    message(FATAL_ERROR "Xbox renderer needs nxdk-gles11 with NXDK_GLES11_WITH_GL4ES=ON")
endif()
# Xbox defines _WIN32 but has no WGL or Windows DLL lifecycle. Bootstrap gl4es
# explicitly in win_xbox.c after glContextInit, using its static GLES loader.
target_compile_definitions(GL PRIVATE NO_INIT_CONSTRUCTOR _WINGDI_)
