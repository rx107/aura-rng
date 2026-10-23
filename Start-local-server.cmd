@echo off
setlocal
cd /d "%~dp0"
echo Star's RNG local server: http://127.0.0.1:8765
echo Keep this window open while playing. Press Ctrl+C to stop.
where py >nul 2>nul
if not errorlevel 1 (
  py -3 server\serve.py --host 127.0.0.1 --port 8765 --data-dir server-data
  goto finished
)
where python >nul 2>nul
if not errorlevel 1 (
  python server\serve.py --host 127.0.0.1 --port 8765 --data-dir server-data
  goto finished
)
echo Python 3.10 or newer is required: https://www.python.org/downloads/windows/
:finished
pause
