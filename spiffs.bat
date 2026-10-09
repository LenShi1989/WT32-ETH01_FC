@echo off
chcp 65001 >nul
setlocal

rem ===========================================================================
rem 產生 SPIFFS 映像檔 (spiffs.bin)
rem   可在網頁「OTA 更新」選「網頁檔 (SPIFFS .bin)」上傳
rem
rem 用法：spiffs.bat [fc ^| rc ^| all]     （預設 all）
rem   fc  -> build\fc_spiffs.bin  （fc_wt32eth01\data）
rem   rc  -> build\rc_spiffs.bin  （rc_d1mini\data）
rem
rem 參數需與 Partition Scheme「Default 4MB with spiffs」一致：
rem   spiffs 分區大小 0x160000、block 4096、page 256
rem 換成其他分區時請修改 SPIFFS_SIZE（例如 Minimal SPIFFS = 0x20000）
rem ===========================================================================

set "SPIFFS_SIZE=0x160000"
set "BLOCK=4096"
set "PAGE=256"

cd /d "%~dp0"
set "OUT=%~dp0build"

rem ---- 找 mkspiffs（Arduino IDE 安裝的 ESP32 工具，或 PATH）----
set "MKSPIFFS="
for /d %%D in ("%LOCALAPPDATA%\Arduino15\packages\esp32\tools\mkspiffs\*") do (
  if exist "%%D\mkspiffs.exe" set "MKSPIFFS=%%D\mkspiffs.exe"
)
if not defined MKSPIFFS (
  for /f "delims=" %%P in ('where mkspiffs 2^>nul') do if not defined MKSPIFFS set "MKSPIFFS=%%P"
)
if not defined MKSPIFFS (
  echo [錯誤] 找不到 mkspiffs.exe
  echo        請先在 Arduino IDE 安裝 esp32 開發板套件，或把 mkspiffs 加入 PATH
  set "FAIL=1"
  goto :end
)

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=all"
if /i not "%TARGET%"=="fc" if /i not "%TARGET%"=="rc" if /i not "%TARGET%"=="all" (
  echo 用法：spiffs.bat [fc ^| rc ^| all]
  set "FAIL=1"
  goto :end
)

echo mkspiffs：%MKSPIFFS%
echo 分區大小：%SPIFFS_SIZE%  block：%BLOCK%  page：%PAGE%
echo.

if not exist "%OUT%" mkdir "%OUT%"
set "FAIL=0"
if /i not "%TARGET%"=="rc" call :build fc_wt32eth01 fc_spiffs.bin
if /i not "%TARGET%"=="fc" call :build rc_d1mini rc_spiffs.bin

echo.
if "%FAIL%"=="0" (
  echo 全部完成。到裝置網頁「OTA 更新」選「網頁檔 ^(SPIFFS .bin^)」上傳對應的檔案。
) else (
  echo 有映像檔產生失敗，請查看上方訊息。
)
goto :end

rem ---- 產生單一映像：%1 = sketch 資料夾，%2 = 輸出檔名 ----
:build
set "SRC=%~dp0%~1\data"
set "BIN=%OUT%\%~2"
if not exist "%SRC%\index.html" (
  echo [錯誤] %SRC% 中沒有 index.html
  set "FAIL=1"
  exit /b
)
echo [%~1] 產生 build\%~2
"%MKSPIFFS%" -c "%SRC%" -b %BLOCK% -p %PAGE% -s %SPIFFS_SIZE% "%BIN%"
if errorlevel 1 (
  echo [錯誤] mkspiffs 失敗（檔案總大小可能超過分區）
  set "FAIL=1"
  exit /b
)
for %%F in ("%BIN%") do echo   完成：%%~zF bytes
echo.
exit /b

:end
rem 從檔案總管雙擊執行時暫停，讓使用者看到結果
echo %CMDCMDLINE% | find /i "%~nx0" >nul && pause
exit /b %FAIL%
