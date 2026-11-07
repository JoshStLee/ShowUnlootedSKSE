set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('ShowUnlootedSKSE')
set_version('1.2.8')
set_license('GPL-3.0')

-- Build one DLL for SE and AE (including AE 1.7.x), without enabling VR.
set_config('skyrim_se', true)
set_config('skyrim_ae', true)
set_config('skyrim_vr', false)

set_languages('c++23')
set_warnings('allextra')
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

target('ShowUnlootedSKSE')
    add_deps('commonlibsse-ng')
    add_rules('commonlibsse-ng.plugin', {
        name        = 'ShowUnlootedSKSE',
        author      = 'LeStLee',
        description = 'Attempt to display recent kills in compass via SKSE',
    })

    add_files('src/**.cpp')
    add_headerfiles('src/**.h')

    add_includedirs(
        'src',
        '$(projectdir)'
    )

    set_pcxxheader('src/pch.h')
