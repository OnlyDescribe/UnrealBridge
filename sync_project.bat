@echo off
setlocal EnableExtensions

set "REPO_ROOT=%~dp0"
set "REPO_ROOT=%REPO_ROOT:~0,-1%"
set "PLUGIN_SOURCE=%REPO_ROOT%\Plugin\UnrealBridge"
set "SKILL_SOURCE=%REPO_ROOT%\.claude\skills\unreal-bridge"

if "%~1"=="" (
    echo Usage: %~nx0 ^<UE project root^>
    echo Example: %~nx0 D:\Path\To\YourProject
    exit /b 2
)

for %%I in ("%~1") do set "PROJECT_ROOT=%%~fI"
set "PLUGIN_TARGET=%PROJECT_ROOT%\Plugins\UnrealBridge"
set "AGENTS_SKILL_TARGET=%PROJECT_ROOT%\.agents\skills\unreal-bridge"
set "CLAUDE_SKILL_TARGET=%PROJECT_ROOT%\.claude\skills\unreal-bridge"

if not exist "%PLUGIN_SOURCE%\UnrealBridge.uplugin" (
    echo ERROR: UnrealBridge plugin source is missing:
    echo   %PLUGIN_SOURCE%
    exit /b 1
)

if not exist "%SKILL_SOURCE%\SKILL.md" (
    echo ERROR: UnrealBridge skill source is missing:
    echo   %SKILL_SOURCE%
    exit /b 1
)

if not exist "%PROJECT_ROOT%\*.uproject" (
    echo ERROR: No .uproject was found under:
    echo   %PROJECT_ROOT%
    exit /b 1
)

echo Syncing UnrealBridge from:
echo   %REPO_ROOT%
echo.

set "SOURCE_COMMIT=unknown"
for /f "delims=" %%C in ('git -C "%REPO_ROOT%" rev-parse HEAD 2^>nul') do set "SOURCE_COMMIT=%%C"
set "SOURCE_DIRTY="
git -C "%REPO_ROOT%" diff-index --quiet HEAD -- 2>nul
if errorlevel 1 set "SOURCE_DIRTY=1"
git -C "%REPO_ROOT%" ls-files --others --exclude-standard 2>nul | findstr /R "." >nul
if not errorlevel 1 set "SOURCE_DIRTY=1"
if defined SOURCE_DIRTY set "SOURCE_COMMIT=%SOURCE_COMMIT%-dirty"

echo [1/3] Plugin
robocopy "%PLUGIN_SOURCE%" "%PLUGIN_TARGET%" /MIR /XD Binaries Intermediate Saved /XF *.pdb .unrealbridge-source-commit /R:2 /W:1 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 (
    echo ERROR: Plugin sync failed.
    exit /b 1
)
> "%PLUGIN_TARGET%\.unrealbridge-source-commit" echo %SOURCE_COMMIT%
findstr /X /L /C:"%SOURCE_COMMIT%" "%PLUGIN_TARGET%\.unrealbridge-source-commit" >nul
if errorlevel 1 (
    echo ERROR: Could not write or verify the plugin version stamp.
    exit /b 1
)

echo [2/3] .agents skill
robocopy "%SKILL_SOURCE%" "%AGENTS_SKILL_TARGET%" /MIR /XD __pycache__ /XF *.pyc .unrealbridge-source-commit /R:2 /W:1 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 (
    echo ERROR: .agents skill sync failed.
    exit /b 1
)
> "%AGENTS_SKILL_TARGET%\.unrealbridge-source-commit" echo %SOURCE_COMMIT%
findstr /X /L /C:"%SOURCE_COMMIT%" "%AGENTS_SKILL_TARGET%\.unrealbridge-source-commit" >nul
if errorlevel 1 (
    echo ERROR: Could not write or verify the .agents skill version stamp.
    exit /b 1
)

echo [3/3] .claude skill
robocopy "%SKILL_SOURCE%" "%CLAUDE_SKILL_TARGET%" /MIR /XD __pycache__ /XF *.pyc .unrealbridge-source-commit /R:2 /W:1 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 (
    echo ERROR: .claude skill sync failed.
    exit /b 1
)
> "%CLAUDE_SKILL_TARGET%\.unrealbridge-source-commit" echo %SOURCE_COMMIT%
findstr /X /L /C:"%SOURCE_COMMIT%" "%CLAUDE_SKILL_TARGET%\.unrealbridge-source-commit" >nul
if errorlevel 1 (
    echo ERROR: Could not write or verify the .claude skill version stamp.
    exit /b 1
)

if not exist "%PROJECT_ROOT%\Saved\UnrealBridge" mkdir "%PROJECT_ROOT%\Saved\UnrealBridge"
if not exist "%PROJECT_ROOT%\Saved\UnrealBridge" (
    echo ERROR: Could not create the project-local UnrealBridge metadata directory.
    exit /b 1
)
> "%PROJECT_ROOT%\Saved\UnrealBridge\source-root.txt" echo %REPO_ROOT%
findstr /X /L /C:"%REPO_ROOT%" "%PROJECT_ROOT%\Saved\UnrealBridge\source-root.txt" >nul
if errorlevel 1 (
    echo ERROR: Could not write or verify the UnrealBridge source repository path.
    exit /b 1
)

echo.
echo UnrealBridge sync complete.
echo   Plugin:        %PLUGIN_TARGET%
echo   .agents skill: %AGENTS_SKILL_TARGET%
echo   .claude skill: %CLAUDE_SKILL_TARGET%
echo   Source commit: %SOURCE_COMMIT%
exit /b 0
