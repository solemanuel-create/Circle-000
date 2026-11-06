#pragma once

#include <string>
#include <vector>

// Nomes amigaveis dos filtros DirectShow de captura, na mesma ordem dos
// indices que cv::VideoCapture(cap, CAP_DSHOW) aceita. TU separado porque
// dshow.h puxa macros que os cabecalhos do OpenCV e do openFrameworks nao
// engolem juntos.
std::vector<std::string> cvListCameras();

// A mesma coisa do outro lado: as fontes do Media Foundation, na ordem que
// cv::VideoCapture(cap, CAP_MSMF) indexa. NAO e a lista acima reordenada - e
// uma enumeracao propria, com conteudo e ordem proprios, entao o numero 2 de
// uma lista nao e o numero 2 da outra. O indice so vale dentro da lista do
// backend com que ele foi usado.
std::vector<std::string> cvListCamerasMf();
