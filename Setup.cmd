@echo off
rem Quick start: checks prerequisites, extracts your disc, builds the
rem SDK and the game, and leaves Play.cmd behind. Safe to rerun; finished steps
rem are skipped.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup.ps1" %*
set rc=%errorlevel%
if not defined RECOMP_LAUNCHER pause
exit /b %rc%
