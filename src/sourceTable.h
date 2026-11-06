#pragma once

#include "cvModel.h"

#include <string>
#include <vector>

// O transporte e o que de verdade muda quando se troca de fonte: quem abre o
// dispositivo e quem entrega o frame. O resto - o nome, a tecla, o modo - e
// incidental. Nada no app deve perguntar "e o Kinect?" quando o que quer saber
// e "fala pelo NuiApi?".
enum CvSrcTransport { TRANS_CAPTURE = 0, TRANS_NUI = 1 };

// O registro completo de uma fonte. Antes disto cada verdade vivia num sitio:
// o nome no switch de sourceName(), o token no if de chooseSourceFromEnv(), a
// string procurada no meio do openSource(), a tecla nos quatro ifs do F1..F4, a
// reserva do indice zero dentro da busca, o que o 'c' e o que o 'v' fazem em
// tres ramos diferentes e a nota do modo no meio do HUD. Uma fonte nova e uma
// linha aqui.
struct CvSourceRow {
	int id;					// o CvSource desta linha
	const char* name;		// o que o HUD le
	int key;				// OF_KEY_F1..F4
	const char* envToken;	// o valor de CV0_SRC que chega nesta linha

	// o que se procura na lista do backend: null = nao ha lista a procurar
	// (NuiApi), string vazia = "a primeira que nao seja um filtro virtual
	// conhecido". Os dois significados sao diferentes e por isso sao distintos
	// aqui, nao dois "" ambiguos.
	const char* needle;

	CvSrcTransport transport;

	// a integrada tem reserva: se a busca nao achar nada, abre o indice zero.
	// Sem isto o F1 daria ao palco a primeira camera virtual da sala.
	bool fallbackIndex0;

	// nota de "esta tecla nao tem o que fazer nesta fonte". null = a tecla
	// existe aqui e age. Isto era um if com string literal dentro do keyPressed.
	const char* backendNote;	// c
	const char* modeNote;		// v

	// o modo que o sensor nao aceita pedido: o 1473 e 640x480 por definicao.
	// null = o modo e o que o nosso codigo pede ao cv::VideoCapture.
	const char* fixedMode;
};

class SourceTable {

public:
	SourceTable();

	int rowCount() const { return (int)rows_.size(); }

	// o id vem do CvParams vivo. Se nao houver linha para ele devolve-se a
	// linha de acusacao, nunca um nome inventado: e assim que uma fonte
	// acrescentada ao enum sem linha aqui aparece na tela em vez de aparecer
	// mentindo.
	const CvSourceRow& byId(int id) const;
	const CvSourceRow* byKey(int key) const;
	const CvSourceRow* byEnv(const std::string& token) const;

	const char* name(int id) const { return byId(id).name; }
	CvSrcTransport transport(int id) const { return byId(id).transport; }
	const char* needle(int id) const { return byId(id).needle; }
	bool hasFallbackIndex0(int id) const { return byId(id).fallbackIndex0; }
	const char* backendNote(int id) const { return byId(id).backendNote; }
	const char* modeNote(int id) const { return byId(id).modeNote; }

	// as duas linhas de texto que o painel le, derivadas da linha mais o estado
	std::string backendText(const CvParams& p) const;
	std::string modeText(const CvParams& p) const;

	// o passo do modo pedido, no unico lugar onde a escada 640x480 / 1280x720
	// existe. Devolve false quando a fonte nao tem modo a escolher.
	bool cycleMode(CvParams& p) const;

private:
	int add(int id, const char* name, int key, const char* envToken,
		const char* needle, CvSrcTransport transport, bool fallbackIndex0,
		const char* backendNote, const char* modeNote, const char* fixedMode);

	std::vector<CvSourceRow> rows_;
	// id -> linha; -1 e o vazio que cai na linha de acusacao
	int rowOfId_[SRC_COUNT];
	CvSourceRow accusacao_;
};
