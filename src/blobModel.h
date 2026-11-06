#pragma once

#include "cvModel.h"

#include <string>

// O que um contorno vira quando sai da maquina e entra no render.
//
// Ate aqui o contrato era um vector<float> de 128 e um `i * 4` repetido em duas
// rotas: o slot nao dizia quem era o corpo, e o 32 estava escrito duas vezes -
// uma no cvModel e outra no #define do frag - sem nada conferir uma com a outra.
// Este ficheiro traz as duas coisas que faltavam:
//
//   identidade   cada slot carrega um id que sobrevive de um frame para o outro.
//                Na camera o casamento e pelo centro; no sintetico o indice ja
//                e a identidade e vem de fora. Id novo quer dizer corpo que
//                entrou, nao corpo que mudou de coluna.
//   um 32 so     verifyFrag le o #define do frag contra o CV_MAX_BLOBS do C++ e
//                um desacordo vira aviso no HUD, nao uma tela que mente.
//
// A ordem de saida e a de sempre: maiores primeiro na camera, indice na fonte
// sintetica. Nao e conservadorismo - o frag varre uBlobs e para no primeiro
// retangulo que bate, entao reordenar os slots trocaria a imagem onde dois
// corpos se sobrepoe. A identidade e uma anotacao ao lado do slot, nunca uma
// reordenacao, e por isso o upload fica byte a byte o que sempre foi.

// O vec4 do frag: x0,y0 e o canto superior esquerdo, x1,y1 o inferior direito,
// em normalizado e ja com o crescimento do envelope. b.x b.y b.z b.w.
struct CvBlobBox {
	float x0, y0, x1, y1;
};

static_assert(sizeof(CvBlobBox) == 4 * sizeof(float),
	"CvBlobBox tem de ser quatro floats seguidos: e isto que vai para uBlobs");

class BlobRegistry {

public:

	BlobRegistry();

	// troca de fonte ou reseed: o que estava la nao e o que esta agora
	void reset();

	// um frame comeca no slot zero e acaba no endFrame, que e o unico sitio que
	// publica os centros para o casamento do frame seguinte
	void beginFrame();

	// escreve o slot e decide a identidade. idHint > 0 e para quem ja tem
	// identidade propria - o sintetico passa o indice do rect e o registo nao
	// procura centro nenhum. Zero deixa o registo casar pelo centro.
	int push(const CvBlobBox& box, const ofRectangle& src, int idHint);

	void endFrame();

	int count() const { return n_; }
	const CvBlobBox& boxAt(int i) const;
	const ofRectangle& srcAt(int i) const;

	// as duas unicas coisas que hoje nenhum chamador le mas que sao o motivo do
	// ficheiro: o que o glitch vai usar para nao piscar, e o que a sonda imprime
	// para provar que um corpo parado conserva o numero
	int idAt(int i) const;
	int ageAt(int i) const;

	// quantos slots vieram de um id do frame anterior e quantos nasceram agora
	int matched() const { return matched_; }
	int born() const { return born_; }

	// o que vai para o uniform uBlobs: um vec4 por slot, na ordem dos slots.
	// Ninguem mais faz contas de i * 4 nem aponta para dentro de um vector.
	const float* upload() const { return &boxes_[0].x0; }

	// contrato com o frag. 0 = bate, 1 = ficheiro nao lido, 2 = divergente. Os
	// tres estados sao os mesmos do contrato de efeitos porque em cena as duas
	// respostas pedem acoes diferentes: uma e "confia", outra e "corre".
	static int verifyFrag(const std::string& fragText, std::string& note);

private:

	int matchId(float cx, float cy, int forced, int& ageOut);

	CvBlobBox boxes_[CV_MAX_BLOBS];
	ofRectangle srcs_[CV_MAX_BLOBS];
	int ids_[CV_MAX_BLOBS];
	int ages_[CV_MAX_BLOBS];
	int n_;

	// do frame anterior so interessa o que serve para casar: centro, id e idade
	float pcx_[CV_MAX_BLOBS];
	float pcy_[CV_MAX_BLOBS];
	int pids_[CV_MAX_BLOBS];
	int page_[CV_MAX_BLOBS];
	bool taken_[CV_MAX_BLOBS];
	int pn_;

	int nextId_;
	int matched_;
	int born_;
};
