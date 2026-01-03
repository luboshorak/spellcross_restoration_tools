@ECHO OFF

rem ---------------------------------------------------------
rem --- Spellcross *.FS dearchivator by Stanislav Maslan. ---
rem --- This script decodes all *.FS archives in folder.  ---
rem --- usage: spell_extractfs_all.bat [folder_path]      ---
rem ---------------------------------------------------------

SET errn=0 
IF EXIST spell_extractfs.exe GOTO PROK
:ERR2
@ECHO Spellcross *.FS dearchivator by Stanislav Maslan.
@ECHO This script decodes all *.FS archives in folder.
@ECHO usage: spell_extractfs_all.bat [folder_path]
@ECHO.
IF NOT EXIST spell_extractfs.exe @ECHO Error - missing "spell_extractfs.exe" utility!
IF %errn%==1 @ECHO Error - no *.FS archive found in folder!
GOTO ERR
:PROK


SET filter=*.FS
SET lzpath=%%f
IF (%1)==() GOTO NOPATH
SET filter=%1\*.FS
SET lzpath=%1\%%f
:NOPATH


IF NOT EXIST %filter% SET errn=1 & GOTO ERR2


FOR /f %%f IN ('dir %filter% /b') DO spell_extractfs.exe %lzpath%


:ERR
