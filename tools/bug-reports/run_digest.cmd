@echo off
REM Wrapper for the Bonesaw bug report digest, run by Windows Task Scheduler.
REM
REM Exists so the scheduled task has one stable thing to call, and so every run
REM leaves a trace. A digest that quietly stops running is the failure mode that
REM matters here: reports keep accumulating in the database and nobody notices
REM the Discord channel has gone silent because silence looks exactly like "no
REM bugs today".
REM
REM Output is appended to bug_digest.log next to this file. Reports themselves
REM are never at risk from a failed run -- rows are only marked posted after
REM Discord accepts them, so anything that fails here is retried next run.

setlocal
set REPO=A:\wow-bonesaw
set PY=C:\Users\jeremy\AppData\Local\Programs\Python\Python313\python.exe
set LOG=%REPO%\tools\bug-reports\bug_digest.log

cd /d "%REPO%" || exit /b 1

for /f "tokens=* usebackq" %%t in (`powershell -NoProfile -Command "Get-Date -Format 'yyyy-MM-dd HH:mm:ss'"`) do set NOW=%%t
>>"%LOG%" echo [%NOW%] running digest
>>"%LOG%" 2>&1 "%PY%" tools\bug-reports\bug_digest.py
>>"%LOG%" echo [%NOW%] exit code %ERRORLEVEL%

endlocal
