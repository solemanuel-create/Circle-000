#include "effectTable.h"

// Os tres nomes e os tres inteiros abaixo estavam em dois lugares que podiam
// discordar: o switch de effectName() mostrava um nome e o cast `(int)params.effect`
// mandava outro para o shader, sem nada entre eles. Os shaderId sao aqui os
// valores que o blobfx.frag compara hoje - copiados dele, nao deduzidos.
EffectTable::EffectTable() {

	for (int i = 0; i < CV_EFFECT_COUNT; i++) rowOfId_[i] = -1;

	add(CV_EFFECT_INVERT, "INVERT", '1', 0);
	add(CV_EFFECT_BW_THRESHOLD, "BW UMBRAL", '2', 1);
	add(CV_EFFECT_BW_DITHER, "BW DITHER", '3', 2);

	accusacao_.id = -1;
	accusacao_.name = "?? efeito sem linha na tabela";
	accusacao_.key = 0;
	// o 0 e o efeito default: um id que nao existe na tabela nao vai para o
	// shader como lixo, vira o primeiro ramo, que e o que o app faz no setup
	accusacao_.shaderId = 0;
}

int EffectTable::add(int id, const char* name, int key, int shaderId) {
	CvEffectRow r;
	r.id = id;
	r.name = name;
	r.key = key;
	r.shaderId = shaderId;
	rows_.push_back(r);
	rowOfId_[id] = (int)rows_.size() - 1;
	return (int)rows_.size() - 1;
}

const CvEffectRow& EffectTable::byId(int id) const {
	if (id >= 0 && id < CV_EFFECT_COUNT && rowOfId_[id] >= 0) return rows_[rowOfId_[id]];
	return accusacao_;
}

const CvEffectRow* EffectTable::byKey(int key) const {
	if (!key) return nullptr;
	for (size_t i = 0; i < rows_.size(); i++) {
		if (rows_[i].key == key) return &rows_[i];
	}
	return nullptr;
}

int EffectTable::shaderId(int id) const {
	return byId(id).shaderId;
}

bool EffectTable::verifyFrag(const std::string& fragText, std::string& note) const {

	if (fragText.empty()) {
		note = "frag nao lido: contrato de efeitos nao verificado";
		return false;
	}

	bool ok = true;
	for (size_t i = 0; i < rows_.size(); i++) {
		char probe[32];
		sprintf_s(probe, sizeof(probe), "uEffect == %d", rows_[i].shaderId);
		if (fragText.find(probe) == std::string::npos) {
			ok = false;
			note += (note.empty() ? "" : ", ");
			note += std::string(rows_[i].name) + " (uEffect=="
				+ ofToString(rows_[i].shaderId) + ") nao existe no frag";
		}
	}

	// dois efeitos com o mesmo inteiro e a mesma coisa no shader com nomes
	// diferentes na tela, que e exatamente a specie de mentira que a tabela
	// existe para matar
	for (size_t i = 0; i < rows_.size() && ok; i++) {
		for (size_t j = i + 1; j < rows_.size() && ok; j++) {
			if (rows_[i].shaderId == rows_[j].shaderId) {
				ok = false;
				note = std::string(rows_[i].name) + " e " + rows_[j].name
					+ " declaram o mesmo uEffect==" + ofToString(rows_[i].shaderId);
			}
		}
	}

	return ok;
}
