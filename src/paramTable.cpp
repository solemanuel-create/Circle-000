#include "paramTable.h"

#include "ofMain.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {
	// as duas colunas do painel alinham por largura fixa, porque em cena o que
	// se procura e o numero que mudou, nao a palavra que o nomeia
	const int COL_NAME = 17;
	const int COL_VAL = 9;
	const int COL_RANGE = 14;
	const int LINE_H = 13;

	const char* noKey = "-";
}

// -----------------------------------------------------------------------------
// As faixas abaixo sao as que o codigo das teclas ja usava, movidas sem
// mudanca. O que a tabela acrescenta e a visibilidade: uma linha com keys "-"
// e um parametro que hoje so se ajusta recompilando, e isso estava escondido
// em prosa no HUD.
// -----------------------------------------------------------------------------
ParamTable::ParamTable() {

	for (int i = 0; i < P_COUNT; i++) rowOfId_[i] = -1;

	addText("fonte", "fonte pedida", "F1 F2 F3 F4", TXT_SOURCE);
	addText("fonte", "backend", "c", TXT_BACKEND);
	addText("fonte", "modo pedido", "v", TXT_MODE);
	addText("fonte", "efeito", "1 2 3", TXT_EFFECT);
	addInt("fonte", "fps pedido", noKey, -1, 1.f, 60.f, 5.f, &CvParams::camFps);
	addInt("fonte", "indice disp.", noKey, -1, -1.f, 31.f, 1.f, &CvParams::deviceId);

	addInt("porta", "perto mm", ", .", P_NEAR, 400.f, 3000.f, 100.f, &CvParams::kinectNearMM);
	// o piso nao e um numero: e a outra borda mais 300 mm. E a unica linha da
	// tabela com piso dependente, e e por isso que ela existe.
	addIntLo("porta", "longe mm", "[ ]", P_FAR, 4000.f, 250.f,
		&CvParams::kinectNearMM, 300.f, &CvParams::kinectFarMM);

	addInt("maquina", "largura rastreo", "z x", P_TRACKW, 160.f, 1280.f, 160.f, &CvParams::trackW);
	addFloat("maquina", "limiar movim.", "[ ]", P_MOTION, 2.f, 120.f, 2.f, 0, &CvParams::motionThresh);
	addFloat("maquina", "alpha fundo", noKey, -1, 0.f, 0.5f, 0.01f, 2, &CvParams::bgAlpha);
	addFloat("maquina", "area minima", noKey, -1, 0.001f, 0.05f, 0.001f, 3, &CvParams::minAreaPct);

	addFloat("render", "umbral", "+ -", P_THRESH, 0.f, 1.f, 0.05f, 2, &CvParams::threshold);
	addFloat("render", "dither bias", "+ -", P_DITHER, -1.f, 1.f, 0.05f, 2, &CvParams::ditherBias);
	addBool("render", "ver maquina", "t", &CvParams::showMask);
	addBool("render", "flipY", "f", &CvParams::flipY);
	addBool("render", "overlay", "d", &CvParams::showOverlay);
	addFloat("render", "raio mouse", noKey, -1, 100.f, 1500.f, 50.f, 0, &CvParams::mouseRadius);
	addFloat("render", "escala perto", noKey, -1, 0.5f, 6.f, 0.1f, 1, &CvParams::scaleNear);
	addFloat("render", "escala longe", noKey, -1, 0.1f, 3.f, 0.1f, 1, &CvParams::scaleFar);
	addFloat("render", "velocidade", noKey, -1, 0.f, 300.f, 10.f, 0, &CvParams::speed);

	addBool("ganhos", "reativo", "g", &CvParams::reactive);
	addFloat("ganhos", "master", "n m", P_MASTER, 0.f, 2.f, 0.25f, 2, &CvParams::master);
	addFloat("ganhos", "exposicao", noKey, -1, 0.f, 1.f, 0.05f, 2, &CvParams::gainExposure);
	addFloat("ganhos", "crescimento", noKey, -1, 0.f, 1.f, 0.05f, 2, &CvParams::gainGrow);
	addFloat("ganhos", "ganho umbral", noKey, -1, 0.f, 0.5f, 0.02f, 2, &CvParams::gainThresh);
	addFloat("ganhos", "ganho dither", noKey, -1, 0.f, 0.5f, 0.02f, 2, &CvParams::gainDither);
	addFloat("ganhos", "ganho vel", noKey, -1, 0.f, 200.f, 5.f, 0, &CvParams::gainSpeed);
}

// -----------------------------------------------------------------------------
int ParamTable::addFloat(const char* section, const char* name, const char* keys,
	int id, float lo, float hi, float step, int decimals, float CvParams::* slot) {

	CvParamRow r{};
	r.section = section; r.name = name; r.keys = keys; r.id = id;
	r.kind = SLOT_FLOAT; r.txt = TXT_SOURCE;
	r.pf = slot; r.pi = nullptr; r.pb = nullptr;
	r.lo = lo; r.hi = hi; r.step = step; r.decimals = decimals;
	r.loFrom = nullptr; r.loPad = 0.f;
	rows_.push_back(r);
	if (id >= 0 && id < P_COUNT) rowOfId_[id] = (int)rows_.size() - 1;
	return (int)rows_.size() - 1;
}

// -----------------------------------------------------------------------------
int ParamTable::addInt(const char* section, const char* name, const char* keys,
	int id, float lo, float hi, float step, int CvParams::* slot) {

	CvParamRow r{};
	r.section = section; r.name = name; r.keys = keys; r.id = id;
	r.kind = SLOT_INT; r.txt = TXT_SOURCE;
	r.pf = nullptr; r.pi = slot; r.pb = nullptr;
	r.lo = lo; r.hi = hi; r.step = step; r.decimals = 0;
	r.loFrom = nullptr; r.loPad = 0.f;
	rows_.push_back(r);
	if (id >= 0 && id < P_COUNT) rowOfId_[id] = (int)rows_.size() - 1;
	return (int)rows_.size() - 1;
}

// -----------------------------------------------------------------------------
int ParamTable::addIntLo(const char* section, const char* name, const char* keys,
	int id, float hi, float step, const int CvParams::* loFrom, float loPad,
	int CvParams::* slot) {

	CvParamRow r{};
	r.section = section; r.name = name; r.keys = keys; r.id = id;
	r.kind = SLOT_INT; r.txt = TXT_SOURCE;
	r.pf = nullptr; r.pi = slot; r.pb = nullptr;
	// o lo literal fica 0: quem manda e loFrom + loPad, em cada leitura
	r.lo = 0.f; r.hi = hi; r.step = step; r.decimals = 0;
	r.loFrom = loFrom; r.loPad = loPad;
	rows_.push_back(r);
	if (id >= 0 && id < P_COUNT) rowOfId_[id] = (int)rows_.size() - 1;
	return (int)rows_.size() - 1;
}

// -----------------------------------------------------------------------------
int ParamTable::addBool(const char* section, const char* name, const char* keys,
	bool CvParams::* slot) {

	CvParamRow r{};
	r.section = section; r.name = name; r.keys = keys; r.id = -1;
	r.kind = SLOT_BOOL; r.txt = TXT_SOURCE;
	r.pf = nullptr; r.pi = nullptr; r.pb = slot;
	r.lo = 0.f; r.hi = 1.f; r.step = 0.f; r.decimals = 0;
	r.loFrom = nullptr; r.loPad = 0.f;
	rows_.push_back(r);
	return (int)rows_.size() - 1;
}

// -----------------------------------------------------------------------------
int ParamTable::addText(const char* section, const char* name, const char* keys,
	CvTextSlot txt) {

	CvParamRow r{};
	r.section = section; r.name = name; r.keys = keys; r.id = -1;
	r.kind = SLOT_TEXT; r.txt = txt;
	r.pf = nullptr; r.pi = nullptr; r.pb = nullptr;
	r.lo = 0.f; r.hi = 0.f; r.step = 0.f; r.decimals = 0;
	r.loFrom = nullptr; r.loPad = 0.f;
	rows_.push_back(r);
	return (int)rows_.size() - 1;
}

// -----------------------------------------------------------------------------
float ParamTable::floorOf(const CvParams& p, const CvParamRow& r) const {
	if (r.loFrom) return (float)(p.*(r.loFrom)) + r.loPad;
	return r.lo;
}

// -----------------------------------------------------------------------------
float ParamTable::raw(const CvParams& p, const CvParamRow& r) const {
	switch (r.kind) {
	case SLOT_FLOAT: return p.*(r.pf);
	case SLOT_INT: return (float)(p.*(r.pi));
	case SLOT_BOOL: return p.*(r.pb) ? 1.f : 0.f;
	default: return 0.f;
	}
}

// -----------------------------------------------------------------------------
void ParamTable::write(CvParams& p, const CvParamRow& r, float v) const {
	const float lo = floorOf(p, r);
	switch (r.kind) {
	case SLOT_FLOAT:
		p.*(r.pf) = ofClamp(v, lo, r.hi);
		break;
	case SLOT_INT:
		p.*(r.pi) = (int)ofClamp(v, lo, r.hi);
		break;
	default:
		// bool e texto nao tem faixa: mexer neles e troca de estado, e isso
		// continua nas maos de quem sabe o estado seguinte
		break;
	}
}

// -----------------------------------------------------------------------------
float ParamTable::get(const CvParams& p, int id) const {
	if (id < 0 || id >= P_COUNT) return 0.f;
	const int r = rowOfId_[id];
	if (r < 0) return 0.f;
	return raw(p, rows_[r]);
}

// -----------------------------------------------------------------------------
void ParamTable::setAbs(CvParams& p, int id, float v) const {
	if (id < 0 || id >= P_COUNT) return;
	const int r = rowOfId_[id];
	if (r < 0) return;
	write(p, rows_[r], v);
}

// -----------------------------------------------------------------------------
float ParamTable::nudge(CvParams& p, int id, float dir) const {
	if (id < 0 || id >= P_COUNT) return 0.f;
	const int r = rowOfId_[id];
	if (r < 0) return 0.f;
	const CvParamRow& row = rows_[r];
	// o passo multiplicado pelo sinal, nao escolhido entre dois ramos: e a
	// unica forma de o + e o - partilharem exatamente a aritmetica
	write(p, row, raw(p, row) + dir * row.step);
	return raw(p, row);
}

// =========================
int ParamTable::drawPanel(const CvParams& p, const CvParamContext& ctx, int x, int y) const {

	const std::string* texts[4] = { &ctx.source, &ctx.backend, &ctx.mode, &ctx.effect };

	char b[128];
	char val[32];
	char rng[48];

	ofSetColor(200);
	ofDrawBitmapString("parametros  (valor | faixa | tecla)", x, y);
	y += LINE_H;

	const char* section = "";
	for (size_t i = 0; i < rows_.size(); i++) {
		const CvParamRow& r = rows_[i];

		if (strcmp(r.section, section) != 0) {
			section = r.section;
			y += 3;
			ofSetColor(150);
			// o cabecalho em maiusculas e a unica hierarquia visual que o
			// bitmap font permite; ofUpper nao existe nesta instalacao
			char hdr[32];
			snprintf(hdr, sizeof(hdr), "%s", section);
			for (char* c = hdr; *c; ++c) *c = (char)toupper((unsigned char)*c);
			ofDrawBitmapString(hdr, x, y);
			y += LINE_H;
		}

		val[0] = 0;
		switch (r.kind) {
		case SLOT_FLOAT:
			sprintf_s(val, sizeof(val), "%.*f", r.decimals, raw(p, r));
			break;
		case SLOT_INT:
			sprintf_s(val, sizeof(val), "%d", p.*(r.pi));
			break;
		case SLOT_BOOL:
			sprintf_s(val, sizeof(val), "%s", p.*(r.pb) ? "LIGADO" : "deslig");
			break;
		case SLOT_TEXT:
			snprintf(val, sizeof(val), "%s", texts[r.txt]->c_str());
			break;
		}

		if (r.kind == SLOT_FLOAT || r.kind == SLOT_INT) {
			if (r.kind == SLOT_INT) {
				sprintf_s(rng, sizeof(rng), "%d..%d", (int)floorOf(p, r), (int)r.hi);
			} else {
				sprintf_s(rng, sizeof(rng), "%g..%g", floorOf(p, r), r.hi);
			}
		} else {
			// bool e texto nao tem faixa, e um "-" no lugar dela ler-se como
			// uma faixa que alguem esqueceu de preencher
			rng[0] = 0;
		}

		// uma linha esmaea e uma linha sem tecla: em cena a pergunta e sempre
		// "isto mexe-se daqui ou tenho de recompilar", e o cor responde isso
		const bool semTecla = (strcmp(r.keys, noKey) == 0);
		ofSetColor(semTecla ? 120 : 255);

		if (rng[0]) {
			sprintf_s(b, sizeof(b), "%-*s%*s  [%-*s] %s",
				COL_NAME, r.name, COL_VAL, val, COL_RANGE, rng, r.keys);
		} else {
			// sem os colchetes o campo continua a ocupar o mesmo largo, para
			// as teclas de todas as linhas cairem na mesma coluna
			sprintf_s(b, sizeof(b), "%-*s%*s  %-*s %s",
				COL_NAME, r.name, COL_VAL, val, COL_RANGE + 2, "", r.keys);
		}
		ofDrawBitmapString("  " + std::string(b), x, y);
		y += LINE_H;
	}

	ofSetColor(255);
	return y;
}
