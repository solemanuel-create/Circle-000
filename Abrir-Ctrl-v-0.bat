@echo off
rem ==========================================================================
rem  Ctrl-v-0 - abriri o binario de palco com a consola visivel.
rem  A diagnostica deste app sai por stderr (o log do openFrameworks derruba
rem  esta build), entao abrir assim e a unica forma de ver ao vivo o que a
rem  fonte esta a fazer: [fonte], [tempo], [aviso], [dshow], [msmf].
rem
rem  Uso:
rem    Abrir-Ctrl-v-0.bat            webcam integrada (abre em segundos)
rem    Abrir-Ctrl-v-0.bat kinect     Kinect 1473 - paga o cold start de 20-25 s
rem    Abrir-Ctrl-v-0.bat eos        EOS Webcam Utility
rem    Abrir-Ctrl-v-0.bat ndi        NDI Webcam Video
rem
rem  Fechar a consola fecha a peça. Sem argumentos nao define nenhuma
rem  variavel: o app usa a fonte que o codigo dele escolhe sozinho.
rem ==========================================================================
setlocal
cd /d "%~dp0bin"
if not exist ctrlv0.exe (
    echo ERRO: nao ha bin\ctrlv0.exe aqui.
    echo Compilar: MSBuild ctrlv0.vcxproj -p:Configuration=Release -p:Platform=x64
    exit /b 1
)
if /i "%1"=="kinect" set CV0_SRC=kinect
if /i "%1"=="webcam" set CV0_SRC=webcam
if /i "%1"=="eos"    set CV0_SRC=eos
if /i "%1"=="ndi"    set CV0_SRC=ndi
echo (a janela preta que abrir em baixo E a consola: nao e um crash)
ctrlv0.exe
endlocal
