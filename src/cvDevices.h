#pragma once

#include <string>
#include <vector>

// Nomes amigaveis dos filtros DirectShow de captura, na mesma ordem dos
// indices que cv::VideoCapture(cap, CAP_DSHOW) aceita. TU separado porque
// dshow.h puxa macros que os cabecalhos do OpenCV e do openFrameworks nao
// engolem juntos.
std::vector<std::string> cvListCameras();
