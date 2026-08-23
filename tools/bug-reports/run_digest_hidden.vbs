' Launch run_digest.cmd with no console window.
'
' The scheduled task used to call run_digest.cmd directly, which meant a black
' console window stealing focus every fifteen minutes, on top of whatever the
' user was doing. There is no /Quiet for schtasks that fixes this: the task
' runs "Interactive only" (it has to -- the digest reaches the database through
' docker exec, so it cannot work when nobody is logged on), and an interactive
' task showing a console is Windows working as designed.
'
' The usual workaround is "Run whether user is logged on or not", but that
' needs stored credentials and would break the docker dependency anyway. A
' WScript shim with window style 0 is the honest fix: same command, same exit
' path, no window.
'
' bWaitOnReturn is False so the shim exits immediately and the task does not
' sit "Running" for the life of the digest.

Dim shell
Set shell = CreateObject("WScript.Shell")
shell.Run """A:\wow-bonesaw\tools\bug-reports\run_digest.cmd""", 0, False
