param([switch]$Run, [switch]$Clean, [switch]$Test)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if ($Clean) {
        Remove-Item -LiteralPath (Join-Path $PSScriptRoot 'worldbox.exe') -Force -ErrorAction SilentlyContinue
        return
    }

    # Use the same MinGW toolchain for the application and raylib.
    $compiler = (Get-Command g++ -ErrorAction Stop).Source
    $toolchain = Split-Path $compiler
    $gcc = Join-Path $toolchain 'gcc.exe'
    $ar = Join-Path $toolchain 'ar.exe'
    $raylib = Join-Path $PSScriptRoot 'build/deps/raylib'
    if (!(Test-Path "$raylib/src/raylib.h")) {
        & git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git $raylib
        if ($LASTEXITCODE -ne 0) { throw 'Could not download raylib 5.5.' }
    }

    $library = Join-Path $PSScriptRoot 'build/libraylib.a'
    if (!(Test-Path $library)) {
        $objects = @()
        foreach ($module in @('rcore', 'rshapes', 'rtextures', 'rtext', 'rmodels', 'utils', 'raudio', 'rglfw')) {
            $object = Join-Path $PSScriptRoot "build/$module.o"
            & $gcc -c "$raylib/src/$module.c" -o $object -O2 -std=c99 -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 "-I$raylib/src" "-I$raylib/src/external/glfw/include"
            if ($LASTEXITCODE -ne 0) { throw "Could not compile raylib module $module." }
            $objects += $object
        }
        & $ar rcs $library @objects
        if ($LASTEXITCODE -ne 0) { throw 'Could not archive raylib.' }
    }

    if ($Test) {
        & $compiler -Wall -Wextra -std=c++17 -Iinclude/core -isystem "$raylib/src" tests/render_manager_test.cpp src/core/render_manager.cpp -o build/render-test.exe $library -lopengl32 -lgdi32 -lwinmm -static-libgcc -static-libstdc++
        if ($LASTEXITCODE -ne 0) { throw 'Could not compile rendering regression test.' }
        & ./build/render-test.exe
        if ($LASTEXITCODE -ne 0) { throw 'Rendering regression test failed.' }
        return
    }

    $includes = @('include', 'include/core', 'include/gfx', 'include/world', 'include/entities', 'include/civ') | ForEach-Object { "-I$_" }
    $sources = @(Get-ChildItem -Path src -Filter '*.cpp' -Recurse | ForEach-Object { $_.FullName })
    & $compiler -Wall -Wextra -std=c++17 -O2 @includes -isystem "$raylib/src" @sources -o worldbox.exe $library -lopengl32 -lgdi32 -lwinmm -static-libgcc -static-libstdc++
    if ($LASTEXITCODE -ne 0) { throw 'WorldBox compilation failed.' }
    Write-Host 'Build successful: worldbox.exe'

    if ($Run) {
        & ./worldbox.exe
        if ($LASTEXITCODE -ne 0) { throw "WorldBox exited with code $LASTEXITCODE." }
    }
} finally {
    Pop-Location
}
