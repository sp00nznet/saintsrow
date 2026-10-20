@echo off
rem Quick start: checks prerequisites, extracts your disc, builds the SDK and the
rem game, and leaves Play.cmd behind. Safe to rerun; finished steps are skipped.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup.ps1"
pause
