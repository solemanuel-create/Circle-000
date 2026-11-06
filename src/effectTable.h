#pragma once

#include "cvModel.h"

#include <string>
#include <vector>

// O contrato com o uEffect do blobfx.frag. O inteiro que vai para o shader nao
// e a posicao da linha na tabela: e um dado declarado dela, e e isso que faz
// inserir um efeito no meio deixar de empurrar todos os outros em silencio.
struct CvEffectRow {
	int id;			// o CvEffect desta linha
	const char* name;	// o que o HUD le
	int key;			// '1', '2', '3'
	int shaderId;		// o inteiro que o frag compara
};

class EffectTable {

public:
	EffectTable();

	int rowCount() const { return (int)rows_.size(); }

	const CvEffectRow& byId(int id) const;
	const CvEffectRow* byKey(int key) const;

	const char* name(int id) const { return byId(id).name; }
	// o unico caminho legitimo entre o CvParams e o uniform: um id que a tabela
	// nao conhece cai no 0 em vez de ir para o shader como lixo
	int shaderId(int id) const;

	// Liga a tabela ao texto do frag: cada shaderId declarado tem de aparecer
	// la como "uEffect == N". Sem esta linha a tabela pode dizer tres efeitos
	// e o shader executar dois. Devolve false e escreve em note o que falta.
	bool verifyFrag(const std::string& fragText, std::string& note) const;

private:
	int add(int id, const char* name, int key, int shaderId);

	std::vector<CvEffectRow> rows_;
	int rowOfId_[CV_EFFECT_COUNT];
	CvEffectRow accusacao_;
};
