@echo off
rem The setup's archive libraries on Windows: zlib 1.3.1, liblzma (xz 5.8.1)
rem and libarchive 3.7.9 from the same pinned sources as
rem tools/deps/build_archive.sh, each checked by its sha256, built static with
rem clang-cl, CMake and Ninja into deps\archive (include, lib, share\licenses),
rem where CMakeLists.txt finds them. No prebuilt binaries.
rem   tools\windows\build_deps_archive.cmd
rem Nothing here can wait for input: every program reads from nul, the
rem archives are unpacked by Python (not whichever tar is first on PATH), and each
rem stage says so before it starts, so a stop shows where.
setlocal
cd /d "%~dp0\..\.."
echo stage: Visual Studio's C++ tools
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS=%%i
if not defined VS (echo No Visual Studio with the C++ tools found. & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" <nul >nul || exit /b 1
echo stage: the tools found
where cmake ninja clang-cl python <nul || exit /b 1

set DL=%CD%\build-deps\downloads
set WORK=%CD%\build-deps\win\work
set PREFIX=%CD%\deps\archive
rem CMake reads a backslash in a path as an escape: the prefix with forward slashes
set PREFIXF=%PREFIX:\=/%
if not exist "%DL%" mkdir "%DL%"

echo stage: the sources, checked by sha256
call :fetch zlib-1.3.1.tar.gz https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23 || exit /b 1
call :fetch xz-5.8.1.tar.xz https://github.com/tukaani-project/xz/releases/download/v5.8.1/xz-5.8.1.tar.xz 0b54f79df85912504de0b14aec7971e3f964491af1812d83447005807513cd9e || exit /b 1
call :fetch libarchive-3.7.9.tar.xz https://www.libarchive.org/downloads/libarchive-3.7.9.tar.xz ed8b5732e4cd6e30fae909fb945cad8ff9cb7be5c6cdaa3944ec96e4a200c04c || exit /b 1
echo sources: all three match their pins

echo stage: unpacking
if exist "%WORK%" rmdir /s /q "%WORK%"
if exist "%PREFIX%" rmdir /s /q "%PREFIX%"
mkdir "%WORK%" "%PREFIX%"
for %%f in (zlib-1.3.1.tar.gz xz-5.8.1.tar.xz libarchive-3.7.9.tar.xz) do (
  python -I -c "import sys, tarfile; tarfile.open(sys.argv[1]).extractall(sys.argv[2], filter='data')" "%DL%\%%f" "%WORK%" <nul || exit /b 1
)
echo unpacked

set COMMON=-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl "-DCMAKE_INSTALL_PREFIX=%PREFIXF%" "-DCMAKE_PREFIX_PATH=%PREFIXF%" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_POLICY_VERSION_MINIMUM=3.5

set OPTS=-DZLIB_BUILD_EXAMPLES=OFF
echo stage: zlib
call :build "%WORK%\zlib-1.3.1" || exit /b 1
rem only the static zlib: its import library and DLL would be picked first
del /q "%PREFIX%\lib\zlib.lib" "%PREFIX%\bin\zlib*.dll" 2>nul
echo built zlib

set OPTS=-DBUILD_SHARED_LIBS=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF -DXZ_DOC=OFF -DXZ_NLS=OFF
echo stage: liblzma
call :build "%WORK%\xz-5.8.1" || exit /b 1
echo built liblzma

set OPTS=-DBUILD_SHARED_LIBS=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF -DENABLE_TEST=OFF -DENABLE_INSTALL=ON -DENABLE_ZLIB=ON -DENABLE_LZMA=ON -DENABLE_BZip2=OFF -DENABLE_ZSTD=OFF -DENABLE_LZ4=OFF -DENABLE_LIBB2=OFF -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF -DENABLE_NETTLE=OFF -DENABLE_CNG=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF -DENABLE_ICONV=OFF -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_LZO=OFF "-DZLIB_LIBRARY=%PREFIXF%/lib/zlibstatic.lib" "-DZLIB_INCLUDE_DIR=%PREFIXF%/include"
echo stage: libarchive
call :build "%WORK%\libarchive-3.7.9" || exit /b 1
echo built libarchive
rem Their licences, for the package (linked into chi.exe, so no DLL carries them).
echo stage: licences
call :licence libarchive-3.7.9 COPYING || exit /b 1
call :licence zlib-1.3.1 LICENSE || exit /b 1
call :licence xz-5.8.1 COPYING || exit /b 1
call :licence xz-5.8.1 COPYING.0BSD || exit /b 1
echo prefix: %PREFIX%
exit /b 0

:fetch
rem %1 the file, %2 its address, %3 its pinned sha256
if not exist "%DL%\%1" curl -sSfL --max-time 300 -o "%DL%\%1" %2 <nul || exit /b 1
for /f "usebackq delims=" %%h in (`powershell -NoProfile -NonInteractive -Command "(Get-FileHash -Algorithm SHA256 '%DL%\%1').Hash.ToLower()"`) do set GOT=%%h
if not "%GOT%"=="%3" (echo %1: sha256 %GOT%, pinned %3: refused & exit /b 1)
exit /b 0

:licence
rem %1 a source folder, %2 one licence file in it
if not exist "%PREFIX%\share\licenses\%1" mkdir "%PREFIX%\share\licenses\%1"
copy /y "%WORK%\%1\%2" "%PREFIX%\share\licenses\%1\" >nul || exit /b 1
exit /b 0

:build
rem one library: %1 its source folder, OPTS its options
echo configure %~1
cmake -S "%~1" -B "%~1\_build" %COMMON% %OPTS% <nul || exit /b 1
echo build %~1
cmake --build "%~1\_build" <nul || exit /b 1
echo install %~1
cmake --install "%~1\_build" <nul >nul || exit /b 1
exit /b 0
