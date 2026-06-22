@echo off
echo Starting Understand Anything Dashboard...

:: Clear the problematic Vite cache that causes the 'imports' TypeError
if exist "C:\Users\DELL\.understand-anything\repo\understand-anything-plugin\node_modules\.vite" (
    rmdir /s /q "C:\Users\DELL\.understand-anything\repo\understand-anything-plugin\node_modules\.vite"
)

:: Set the project directory so the dashboard knows where to look for knowledge-graph.json
set GRAPH_DIR=%~dp0

:: Navigate to the REAL path of the dashboard, bypassing the symlink
cd /d "C:\Users\DELL\.understand-anything\repo\understand-anything-plugin\packages\dashboard"

:: Start Vite
npx vite --host 127.0.0.1
