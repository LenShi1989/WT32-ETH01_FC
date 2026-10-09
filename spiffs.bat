@echo off
chcp 65001 >nul
setlocal

rem ===========================================================================
rem 產生 SPIFFS 映像檔 (spiffs.bin)
rem   可在網頁「OTA 更新」選「網頁檔 (SPIFFS .bin)」上傳
rem
rem 用法：spiffs.bat [fc ^| rc ^| all] [分區大小]
rem   第 1 個參數（預設 all）
rem     fc  -> build\fc_spiffs.bin  （fc_wt32eth01\data）
rem     rc  -> build\rc_spiffs.bin  （rc_d1mini\data）
rem   第 2 個參數：SPIFFS 分區大小，必須與燒錄時選的 Partition Scheme 相同
rem     default  0x160000  Default 4MB with spiffs（預設）
rem     min      0x20000   Minimal SPIFFS (Large APPS with OTA)
rem     minimal  0xA0000   Minimal (1.3MB APP/700KB SPIFFS)
rem     noota    0x1E0000  No OTA (2MB APP/2MB SPIFFS)
rem     huge     0xE0000   Huge APP (3MB No OTA/1MB SPIFFS)
rem     也可以直接填數值，例如 0x20000
rem   裝置網頁「OTA 更新」頁會顯示該裝置的 SPIFFS 分區大小
rem
rem 範例：spiffs.bat rc min      （遙控器，Minimal SPIFFS 分區）
rem       spiffs.bat fc          （飛控，Default 分區）
rem ===========================================================================

set "SPIFFS_SIZE=0x160000"
set "BLOCK=4096"
set "PAGE=256"
set "FAIL=0"

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
if /i not "%TARGET%"=="fc" if /i not "%TARGET%"=="rc" if /i not "%TARGET%"=="all" goto :usage

rem ---- 分區大小：名稱或數值 ----
set "SIZE_ARG=%~2"
if /i "%SIZE_ARG%"=="default" set "SIZE_ARG=0x160000"
if /i "%SIZE_ARG%"=="min"     set "SIZE_ARG=0x20000"
if /i "%SIZE_ARG%"=="minimal" set "SIZE_ARG=0xA0000"
if /i "%SIZE_ARG%"=="noota"   set "SIZE_ARG=0x1E0000"
if /i "%SIZE_ARG%"=="huge"    set "SIZE_ARG=0xE0000"
if not "%SIZE_ARG%"=="" set "SPIFFS_SIZE=%SIZE_ARG%"
set "SIZE_DEC=0"
set /a "SIZE_DEC=%SPIFFS_SIZE%" 2>nul
set /a "SIZE_REM=SIZE_DEC %% 4096"
if %SIZE_DEC% LEQ 0 goto :badsize
if not %SIZE_REM%==0 goto :badsize

echo mkspiffs：%MKSPIFFS%
echo 分區大小：%SPIFFS_SIZE%（%SIZE_DEC% bytes）  block：%BLOCK%  page：%PAGE%
echo.

if not exist "%OUT%" mkdir "%OUT%"
if /i not "%TARGET%"=="rc" call :build fc_wt32eth01 fc_spiffs.bin
if /i not "%TARGET%"=="fc" call :build rc_d1mini rc_spiffs.bin

echo.
if "%FAIL%"=="0" (
  echo 全部完成。到裝置網頁「OTA 更新」選「網頁檔 ^(SPIFFS .bin^)」上傳對應的檔案。
  echo 映像大小必須等於裝置的 SPIFFS 分區大小（OTA 頁有顯示），不符時裝置會拒絕。
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
"%MKSPIFFS%" -c "%SRC%" -b %BLOCK% -p %PAGE% -s %SIZE_DEC% "%BIN%"
if errorlevel 1 (
  echo [錯誤] mkspiffs 失敗（網頁檔總大小可能超過分區）
  set "FAIL=1"
  exit /b
)
for %%F in ("%BIN%") do echo   完成：%%~zF bytes
echo.
exit /b

:usage
echo 用法：spiffs.bat [fc ^| rc ^| all] [default ^| min ^| minimal ^| noota ^| huge ^| 0x大小]
echo 範例：spiffs.bat rc min
set "FAIL=1"
goto :end

:badsize
echo [錯誤] 分區大小「%SPIFFS_SIZE%」無效，需為 4096 的倍數，例如 0x160000、0x20000，或 default / min
set "FAIL=1"
goto :end

:end
rem 從檔案總管雙擊執行時暫停，讓使用者看到結果
echo %CMDCMDLINE% | find /i "%~nx0" >nul && pause
exit /b %FAIL%
