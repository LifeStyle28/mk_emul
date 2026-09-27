@echo off
rem Build everything (emulator, tests, test firmware) and run all tests with CTest.
call "%~dp0build.bat" test
