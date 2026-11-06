#include "sourceTable.h"

// As quatro linhas abaixo sao a ordem que ele declarou para F1..F4, e os campos
// sao os valores que estavam espalhados por sete sitios do ofApp. Nada aqui foi
// reescolhido: o que mudou e onde mora, nao o que vale.
SourceTable::SourceTable() {

	for (int i = 0; i < SRC_COUNT; i++) rowOfId_[i] = -1;

	// A integrada procura-se por exclusao, nao por nome: "HD User Facing" e o
	// nome que este laptop entrega hoje, e noutra maquina e outro. A reserva do
	// indice zero so dela, porque cair no zero com o F3 daria ao palco a webcam
	// interna achando que e a EOS.
	add(SRC_WEBCAM, "webcam integrada", OF_KEY_F1, "webcam", "",
		TRANS_CAPTURE, true, nullptr, nullptr, nullptr);

	// O 1473 nao tem lista, nao tem backend DSHOW/MSMF e nao tem modo a pedir:
	// as tres coisas que as teclas c e v fazem aqui viram nota porque nao ha
	// o que fazer com elas.
	add(SRC_KINECT, "kinect 360", OF_KEY_F2, "kinect", nullptr,
		TRANS_NUI, false,
		"c: DSHOW/MSMF so existem nas fontes por cv::VideoCapture",
		"v: o modo do 1473 e fixo 640x480",
		"640x480 fixo");

	// as duas reservas por substring: o que a enumeracao do DirectShow chama de
	// "EOS Webcam Utility Pro" e de "NDI Webcam Video 1" cabe nestas duas palavras
	add(SRC_EOS, "EOS utility", OF_KEY_F3, "eos", "eos",
		TRANS_CAPTURE, false, nullptr, nullptr, nullptr);
	add(SRC_NDI, "NDI", OF_KEY_F4, "ndi", "ndi",
		TRANS_CAPTURE, false, nullptr, nullptr, nullptr);

	accusacao_.id = -1;
	accusacao_.name = "?? fonte sem linha na tabela";
	accusacao_.key = 0;
	accusacao_.envToken = nullptr;
	accusacao_.needle = nullptr;
	accusacao_.transport = TRANS_CAPTURE;
	accusacao_.fallbackIndex0 = false;
	accusacao_.backendNote = nullptr;
	accusacao_.modeNote = "v: esta fonte nao tem modo declarado na tabela";
	accusacao_.fixedMode = nullptr;
}

int SourceTable::add(int id, const char* name, int key, const char* envToken,
	const char* needle, CvSrcTransport transport, bool fallbackIndex0,
	const char* backendNote, const char* modeNote, const char* fixedMode) {

	CvSourceRow r;
	r.id = id;
	r.name = name;
	r.key = key;
	r.envToken = envToken;
	r.needle = needle;
	r.transport = transport;
	r.fallbackIndex0 = fallbackIndex0;
	r.backendNote = backendNote;
	r.modeNote = modeNote;
	r.fixedMode = fixedMode;
	rows_.push_back(r);
	rowOfId_[id] = (int)rows_.size() - 1;
	return (int)rows_.size() - 1;
}

const CvSourceRow& SourceTable::byId(int id) const {
	if (id >= 0 && id < SRC_COUNT && rowOfId_[id] >= 0) return rows_[rowOfId_[id]];
	return accusacao_;
}

const CvSourceRow* SourceTable::byKey(int key) const {
	if (!key) return nullptr;
	for (size_t i = 0; i < rows_.size(); i++) {
		if (rows_[i].key == key) return &rows_[i];
	}
	return nullptr;
}

const CvSourceRow* SourceTable::byEnv(const std::string& token) const {
	if (token.empty()) return nullptr;
	for (size_t i = 0; i < rows_.size(); i++) {
		if (rows_[i].envToken && token == rows_[i].envToken) return &rows_[i];
	}
	return nullptr;
}

// "nui" nao e um backend do cv::VideoCapture: e a assinatura de que o pixel nao
// passou por lista nenhuma. O HUD dizia isto com um if no meio do painel.
std::string SourceTable::backendText(const CvParams& p) const {
	const CvSourceRow& r = byId(p.source);
	if (r.transport == TRANS_NUI) return "nui";
	return p.dshowFirst ? "DSHOW" : "MSMF";
}

std::string SourceTable::modeText(const CvParams& p) const {
	const CvSourceRow& r = byId(p.source);
	if (r.fixedMode) return r.fixedMode;
	return p.camW >= 1280 ? "1280x720" : "640x480";
}

// A escada existe porque nesta camera o 720p cai a 15 fps na luz normal e o
// 640x480 segura os 30; o teto do pedido nao e o que o sensor entrega.
bool SourceTable::cycleMode(CvParams& p) const {
	const CvSourceRow& r = byId(p.source);
	if (r.fixedMode) return false;
	if (p.camW >= 1280) { p.camW = 640; p.camH = 480; }
	else { p.camW = 1280; p.camH = 720; }
	return true;
}
