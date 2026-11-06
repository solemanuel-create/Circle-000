#pragma once

#include "cvModel.h"

#include <string>
#include <vector>

// A tabela e o unico lugar onde a faixa, o passo e a tecla de cada parametro
// existem. Antes dela cada tecla repetia o clamp dentro do proprio ramo e o
// HUD repetia o numero a palavra: tres listas que podiam discordar, e nenhuma
// delas era prova de nada. Aqui o ramo da tecla so diz qual linha se mexe e em
// que direcao.
//
// Os ids abaixo sao o que a tecla usa para nomear a linha. Estao enumerados de
// proposito: uma linha sem id e uma linha que nao tem tecla, e isso e visivel
// no painel em vez de ser uma ausencia que so aparece quando alguem procura.
enum CvParamId {
	P_NEAR = 0,		// porta de distancia, borda perto
	P_FAR,			// porta de distancia, borda longe
	P_MOTION,		// limiar de movimento da webcam
	P_TRACKW,		// largura do rastreio
	P_MASTER,		// master dos ganhos
	P_THRESH,		// umbral do render
	P_DITHER,		// bias do dither do render
	P_COUNT
};

enum CvSlotKind { SLOT_FLOAT, SLOT_INT, SLOT_BOOL, SLOT_TEXT };

// As quatro linhas que nao sao um numero: o valor vivo vem do contexto, porque
// fonte, backend, modo e efeito sao escolhas, nao escalares.
enum CvTextSlot { TXT_SOURCE, TXT_BACKEND, TXT_MODE, TXT_EFFECT };

struct CvParamRow {
	const char* section;
	const char* name;
	const char* keys;
	int id;
	CvSlotKind kind;
	CvTextSlot txt;

	// exatamente um destes aponta dentro do CvParams vivo. Um ponteiro para
	// membro e um deslocamento de compilacao: nao ha alocacao nem lambda, e o
	// parametro continua sendo o que ele sempre foi.
	float CvParams::* pf;
	int CvParams::* pi;
	bool CvParams::* pb;

	float lo, hi, step;
	int decimals;

	// o piso de uma linha pode depender de outra: a borda LONGE nunca pode
	// ficar atras da PERTO. Sem isto a tabela mentiria sobre a propria faixa,
	// que e justamente o que ela existe para nao fazer.
	const int CvParams::* loFrom;
	float loPad;
};

// O que o painel precisa saber do mundo fora dos numeros.
struct CvParamContext {
	std::string source;
	std::string backend;
	std::string mode;
	std::string effect;
};

class ParamTable {

public:
	ParamTable();

	int rowCount() const { return (int)rows_.size(); }
	const CvParamRow& row(int i) const { return rows_[i]; }

	float get(const CvParams& p, int id) const;
	float floorOf(const CvParams& p, const CvParamRow& r) const;

	// escrever ja clampa ao piso e ao teto de verdade: quem chama nao precisa
	// de saber a faixa, e por isso nao ha como um chamamento discordar dela
	void setAbs(CvParams& p, int id, float v) const;

	// dir e +1 ou -1. Esta e a unica forma como uma tecla mexe num numero.
	// Devolve o valor novo para quem quiser mostra-lo.
	float nudge(CvParams& p, int id, float dir) const;

	// desenha o painel por seccao, na ordem das linhas da tabela. Devolve o y
	// livre por baixo dele: quem acrescenta um bloco ao painel nao precisa de
	// saber quantas linhas a tabela tem.
	int drawPanel(const CvParams& p, const CvParamContext& ctx, int x, int y) const;

private:
	int addFloat(const char* section, const char* name, const char* keys, int id,
		float lo, float hi, float step, int decimals, float CvParams::* slot);
	int addInt(const char* section, const char* name, const char* keys, int id,
		float lo, float hi, float step, int CvParams::* slot);
	int addIntLo(const char* section, const char* name, const char* keys, int id,
		float hi, float step, const int CvParams::* loFrom, float loPad, int CvParams::* slot);
	int addBool(const char* section, const char* name, const char* keys,
		bool CvParams::* slot);
	int addText(const char* section, const char* name, const char* keys, CvTextSlot txt);

	// o valor actual de uma linha, seja ela qual for o tipo do slot
	float raw(const CvParams& p, const CvParamRow& r) const;
	// escrever com a faixa desta linha (piso dinamico incluido)
	void write(CvParams& p, const CvParamRow& r, float v) const;

	std::vector<CvParamRow> rows_;
	int rowOfId_[P_COUNT];
};
