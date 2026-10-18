# build notesradar.dll (zig / clang / MSVC all work)
# adjust ZIG if your compiler lives elsewhere
set ZIG=zig
"%ZIG%" cc -target x86_64-windows-gnu -shared -O2 -o notesradar.dll src\notesradar.c
if errorlevel 1 goto :err
echo built notesradar.dll
exit /b 0
:err
echo build failed
exit /b 1
