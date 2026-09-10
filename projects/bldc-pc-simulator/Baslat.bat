@echo off
cd /d "%~dp0"
python run_simulator.py
if errorlevel 1 pause
