# Backup-WinService
This program create backup of files in given directory into "7z" archive. After creating, program checks periodically changes of files and, if necessary, adds them to the archive.
Archivating implemented using bit7z static library (https://github.com/rikyoz/bit7z).

Into source code you can set the following parameters:
* SERVICE_NAME - name for registration service into system
* CONFIG_PATH - path to configure file
* LOG_PATH - path to logging file
* BACKUP_INFO_PATH - path 

After compiling, use this commands:
* service_name.exe install – for install (registration) service into system
* service_name.exe remove – for remove service from system
* service_name.exe start – start service
* service_name.exe stop – stop service

For start use:
```
service_name.exe install
service_name.exe start
```
