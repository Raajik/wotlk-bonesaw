# Ship helper: run the warn+save restart script in a hidden background process,
# teeing all output to a log file that the ship driver polls for the final line.
& 'A:\wow-bonesaw\tools\restart_worldserver.ps1' *> 'A:\wow-bonesaw\tools\restart_ship_log.txt'
