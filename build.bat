@echo off
setlocal

:: -------------------------
:: CONFIG
:: -------------------------
:: Get Godot version dynamically
for /f "delims=" %%V in ('python -c "import version; print(f\"{version.major}.{version.minor}.{version.status}.mono\")"') do set GODOT_VERSION=%%V
set TEMPLATE_DIR=%APPDATA%\Godot\export_templates\%GODOT_VERSION%

:: -------------------------
:: Build the editor
:: -------------------------
echo === Building Godot Editor ===
scons platform=windows target=editor module_mono_enabled=yes || goto :error

echo === Generating Mono glue ===
".\bin\godot.windows.editor.x86_64.mono.exe" --headless --generate-mono-glue modules\mono\glue || goto :error

echo === Building C# assemblies ===
python modules\mono\build_scripts\build_assemblies.py --godot-output-dir=bin --godot-platform=windows || goto :error

:: -------------------------
:: Notify editor ready
:: -------------------------
powershell -command "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.MessageBox]::Show('Editor build finished and ready to use!', 'Godot Build')"

:: -------------------------
:: Build the template
:: -------------------------
echo === Building Windows Debug Template ===
scons platform=windows target=template_debug arch=x86_64 module_mono_enabled=yes || goto :error

:: -------------------------
:: Copy templates
:: -------------------------
echo === Installing export templates ===
mkdir "%TEMPLATE_DIR%" 2>nul

copy /Y ".\bin\godot.windows.template_debug.x86_64.mono.exe" ^
        "%TEMPLATE_DIR%\windows_debug_x86_64.exe"

copy /Y ".\bin\godot.windows.template_debug.x86_64.mono.console.exe" ^
        "%TEMPLATE_DIR%\windows_debug_x86_64.console.exe"

echo Templates installed to:
echo %TEMPLATE_DIR%

echo === DONE ===
pause
exit /b 0

:error
echo.
echo ❌ Build failed. Check output above.
pause
exit /b 1
