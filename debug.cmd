@ECHO OFF

SETLOCAL

:: Ensure prerequisites are met.
IF [%ROOTDIR%] EQU [] (
    CALL setenv.cmd
)

:: Process command-line arguments passed to the script.
:Process_Argument
IF [%1] EQU [] GOTO Default_Arguments
IF /I "%1" == "test_hello" SET TARGET_OUTPUT=test_hello.exe
IF /I "%1" == "test_htable" SET TARGET_OUTPUT=test_htable.exe
IF /I "%1" == "test_lockfree" SET TARGET_OUTPUT=test_lockfree.exe
IF /I "%1" == "test_task" SET TARGET_OUTPUT=test_task.exe
IF /I "%1" == "test_string" SET TARGET_OUTPUT=test_string.exe
SHIFT
GOTO Process_Argument

:: Default any unspecified command-line arguments.
:Default_Arguments
IF [%TARGET_OUTPUT%] EQU [] (
    ECHO No target specified; expected "test_hello", "test_htable", "test_lockfree" or "test_task". Debugging "test_task".
    SET TARGET_OUTPUT=test_task.exe
)
IF NOT EXIST "%OUTPUTDIR%" (
    ECHO Output directory "%OUTPUTDIR%" not found; building...
    CALL build.cmd debug
)
IF EXIST "%OUTPUTDIR%\%TARGET_OUTPUT%" (
    start devenv /debugexe "%OUTPUTDIR%\%TARGET_OUTPUT%"
    ENDLOCAL
    EXIT /b 0
) ELSE (
    ECHO Build failed; aborting debug session.
    GOTO Abort_Debug
)

:Abort_Debug
ENDLOCAL
EXIT /b 1

