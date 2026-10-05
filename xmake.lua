set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('ShowUnlootedSKSE')
set_version('1.0.0')
set_license('GPL-3.0')

set_languages('c++23')
set_warnings('allextra')
set_policy('package.requires_lock', true)
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

option('skyrim_se')
    set_default(false)
    set_showmenu(true)
    set_description('Build for Skyrim Special Edition')
option_end()

option('skyrim_ae')
    set_default(false)
    set_showmenu(true)
    set_description('Build for Skyrim Anniversary Edition')
option_end()

target('ShowUnlootedSKSE')
    add_deps('commonlibsse-ng')

    local runtime = 'se_ae'
    if has_config('skyrim_ae') and not has_config('skyrim_se') then
        runtime = 'ae'
    elseif has_config('skyrim_se') and not has_config('skyrim_ae') then
        runtime = 'se'
    end

    add_rules('commonlibsse-ng.plugin', {
        name        = 'ShowUnlootedSKSE',
        author      = 'LeStLee',
        description = 'Attempt to display recent kills in compass via SKSE',
        runtime     = runtime
    })

    add_files('src/**.cpp')
    add_headerfiles('src/**.h')

    add_includedirs(
        'src',
        '$(projectdir)'
    )

    set_pcxxheader('src/pch.h')

    if has_config('skyrim_se') and not has_config('skyrim_ae') then
        add_defines('ENABLE_SKYRIM_SE')
    elseif has_config('skyrim_ae') and not has_config('skyrim_se') then
        add_defines('ENABLE_SKYRIM_AE')
    else
        add_defines('ENABLE_SKYRIM_SE')
        add_defines('ENABLE_SKYRIM_AE')
    end
