@echo off
setlocal

rem Legacy command-name alias. It accepts the same project-root argument as
rem sync_project.bat and deploys both the plugin and matching skills.

call "%~dp0sync_project.bat" %*
endlocal & exit /b %ERRORLEVEL%
