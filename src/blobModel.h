#pragma once

#include "cvModel.h"

#include <string>

// O que um contorno vira quando sai da maquina e entra no render.
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

// A sonda de procedencia do nascimento (CV0_CHURN). Existe por uma pergunta que
// a contagem de nascimentos nao responde: 22 nascimentos por segundo com um ou
// dois corpos vivos podem ser duas coisas completamente diferentes - o mesmo
// contorno a piscar sobre o corte, que pede debounce no gatilho, ou um corpo
// que se move mais que o limiar de casamento entre dois frames e devolve um id
// novo para quem ja tinha um, que pede limiar. As duas tem remedios opostos e
// escolher entre elas sem esta conta e escolher as cegas.
//
// O que se separa e o motivo de cada id novo:
//   vazio     nao havia contorno nenhum no frame anterior - o corpo desapareceu
//             da maquina, e o renascimento e de verdade
//   longe     havia candidato, mas o mais proximo ficou fora do limiar; os
//             baldes de distancia dizem QUAO fora, e e ai que o limiar se julga
//   ocupado   havia candidato dentro do limiar, mas ja tinha dono neste frame -
//             dois contornos a disputar o mesmo corpo
//   forcado   a rota sintetica, onde o indice do rect e a identidade e o
//             casamento por centro nunca aconteceu
struct CvChurnProbe {

	// um array de contadores, nao um campo por balde: a leitura do HUD e um
	// laco e o que muda no futuro e o numero de baldes, nao a lista de campos
	enum { BALDES = 5 };

	long frames;
	long framesSemContorno;
	long nascidos;
	long casados;

	long porVazio;
	long porLonge;
	long porOcupado;
	long porForcado;

	// distancia normalizada do candidato mais proximo quando o nascimento foi
	// "longe": [10,15) [15,25) [25,50) [50,100) [100,inf) em centecimos de
	// quadro. O primeiro balde comeca no limiar porque abaixo dele o caso ja se
	// chamou ocupado.
	long longe[BALDES];
	// a soma das mesmas distancias, para a media sair sem guardar uma por uma
	double somaLonge;

	// dentro do "longe" ha duas doencas com remedios opostos, e a distancia
	// sozinha nao as separa:
	//   longeLivre   o candidato mais proximo estava livre neste frame - ninguem
	//                o quis, e o limiar e que nao o deixou ser o mesmo corpo
	//   longeTomado  o candidato mais proximo ja tinha dono neste frame - o que
	//                nasceu e um SEGUNDO contorno do mesmo corpo, ou seja a
	//                silhueta fragmentou-se, e subir o limiar la trocaria ids
	// Sem esta conta, os dois vivem no mesmo "longe" e escolher entre limiar e
	// morfologia e escolher as cegas.
	long longeLivre;
	long longeTomado;

	// ----- o gatilho, nao o registo -----
	// A varredura de limiares e a de corte fecharam o diagnostico do pixel: o
	// que resta sao dois pedacos GRANDES do mesmo corpo, a 0,25-0,50 de largura
	// de quadro um do outro, que nenhum kernel fecha. Resta a outra metade da
	// pergunta, e ela nao e do rastreador - e do gatilho, que hoje trata cada
	// nascimento de id como uma entrada de corpo. Estas contas nao mudam nada:
	// registam o nascimento, e N frames depois perguntam se aquele id ainda esta
	// na maquina. E isso que uma nota teria de saber antes de soar.
	//
	//   pulsoVive     o id ainda existia em nasc+PULSO_N - um debounce deixaria
	//                 passar, e e a nota que um corpo de verdade deu
	//   pulsoMorre    o id desapareceu em menos de N frames - o que a saida hoje
	//                 chama entrada e o que um fragmento de um frame vive
	//   pulsoSozinho  dos que vivem: nao havia NEM corpo do frame anterior NEM
	//                 outro recem-nascido deste frame a menos de PULSO_X - a
	//                 regra mais fechada deixaria este passar
	//   pulsoPertoEstab  dos que vivem, havia um contorno do frame anterior a
	//                 menos de PULSO_X: e o segundo braco de um corpo que ja
	//                 estava em cena, e uma nota dele e uma nota repetida
	//   pulsoPertoNovo   dos que vivem, havia OUTRO recem-nascido deste frame
	//                 perto: os dois bracos nasceram juntos, e nenhum dos dois e
	//                 "estabelecido" - uma regra que so olhasse para os velhos
	//                 deixaria os dois soar
	//   pulsoFilaCheia / pulsoPendentes  a sonda acusa-se: a fila de pendentes e
	//                 fixa, e uma janela que termine com pendentes nao contou o
	//                 rabo da propria janela
	enum { PULSO_N = 8, PULSO_FILA = 64, PULSO_NASC = 16 };
	static const float PULSO_X;

	// A curva de sobrevivencia: os mesmos pendentes, resolvidos contra varias
	// idades numa so corrida. Serve a uma pergunta que o N=8 nao responde e que
	// e de palco, nao de contagem - quanto custa cada N em latencia e o que se
	// perde de corpo por ir mais curto. A 30 fps, 1/2/4 frames sao 33/66/133 ms,
	// e so o primeiro e o segundo cabem no orcamento de 120 ms da peca.
	//
	// O degrau tres esta la por uma razao de medida, nao de gosto: o orcamento
	// de 120 ms nao corta entre o 2 e o 4, corta entre o 3 e o 4 (97 ms e
	// 130 ms ao ritmo de 31 fps medido numa corrida de 30 s). Sem o degrau a
	// escolha seria uma interpolacao entre dois numeros, e interpolacao nao e
	// medida.
	enum { CURVA_N = 6 };
	static const int CURVA_IDADE[CURVA_N];
	long curva[CURVA_N];

	// o estado terminal de cada nascimento: ou morre a uma idade, ou sai pela
	// borda de cima da curva. E esta conta, nao a dos vivos, que tem de fechar
	// com os nascimentos - os vivos sao um subconjunto que pode ter saido ja.
	long pulsoSaidos;

	long pulsoVive;
	long pulsoMorre;
	long pulsoSozinho;
	long pulsoPertoEstab;
	long pulsoPertoNovo;
	long pulsoFilaCheia;
	long pulsoPendentes;
};

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

	// ----- sonda de procedencia -----
	// Desligada por omissao: sem setChurnProbe(true) nenhum dos campos abaixo e
	// tocado, nenhum contador se move, e o caminho do casamento fica byte a byte
	// o que sempre foi. E assim que uma medicao nova pode ficar no fonte sem
	// mudar numero nenhum em cena.
	void setChurnProbe(bool ativa) { churnAtivo_ = ativa; }
	const CvChurnProbe& churn() const { return churn_; }

	// O limiar de casamento, em largura de quadro normalizada. Nasce no valor
	// de sempre e so muda se alguem o pedir: e o CV0_BLOBMATCH da tabela de
	// limiares, que existe para o limiar se escolher por conta e nao por
	// adjetivo. Sobrevive a reset() de proposito - um reseed no meio de uma
	// corrida nao pode mudar a condicao que a corrida esta a medir.
	void setMatch(float largura) { match_ = largura; }
	float match() const { return match_; }

	// ----- sonda de sincronia (CV0_SYNC) -----
	// O instante em que este frame foi recebido, no relogio da sonda. Passar um
	// valor > 0 e armar; passar 0 (ou nunca passar nada) e o desarmado, e neste
	// caminho nenhum dos campos abaixo e escrito e nenhuma idade e carimbada.
	//
	// O que se guarda nao e o slot e sim o ID: um corpo nasce num frame e so da
	// nota tres frames depois, e nesses tres frames a ordem por area mudou e o
	// slot zero e outro corpo. Carimbar o slot seria medir o nascimento de um e
	// a nota de outro.
	void setSyncFrameUs(long long us) { syncUs_ = us; }
	long long bornUsAt(int i) const;

	// o que vai para o uniform uBlobs: um vec4 por slot, na ordem dos slots.
	// Ninguem mais faz contas de i * 4 nem aponta para dentro de um vector.
	const float* upload() const { return &boxes_[0].x0; }

	// contrato com o frag. 0 = bate, 1 = ficheiro nao lido, 2 = divergente. Os
	// tres estados sao os mesmos do contrato de efeitos porque em cena as duas
	// respostas pedem acoes diferentes: uma e "confia", outra e "corre".
	static int verifyFrag(const std::string& fragText, std::string& note);

private:

	int matchId(float cx, float cy, int forced, int& ageOut);

	// o que acontece DEPOIS de um nascimento estar decidido: percorre os centros
	// do frame anterior outra vez, desta vez sem o limiar, so para saber quao
	// longe estava o candidato mais proximo. Nao toca em id, idade nem slot.
	void churnRegista(float cx, float cy, int id, bool forcado);

	// o ledger do gatilho: um nascimento entra com o carimbo do relogio da sonda
	// e com as duas vizinhancas vistas no instante em que nasceu, e sai de la
	// resolvido PULSO_N frames depois, quando se sabe se o id ainda existe
	struct CvChurnPendente {
		int id;
		long quando;
		int proximo;		// proxima idade da curva a testar
		bool pertoEstab;
		bool pertoNovo;
	};
	CvChurnPendente pend_[CvChurnProbe::PULSO_FILA];
	int pendN_;
	float nascCx_[CvChurnProbe::PULSO_NASC];
	float nascCy_[CvChurnProbe::PULSO_NASC];
	int nascN_;

	void churnPulsoRegista(float cx, float cy, int id, bool pertoEstab, bool pertoNovo);
	void churnPulsoResolve();

	CvBlobBox boxes_[CV_MAX_BLOBS];
	ofRectangle srcs_[CV_MAX_BLOBS];
	int ids_[CV_MAX_BLOBS];
	int ages_[CV_MAX_BLOBS];
	long long bornUs_[CV_MAX_BLOBS];
	int n_;

	// do frame anterior so interessa o que serve para casar: centro, id e idade
	float pcx_[CV_MAX_BLOBS];
	float pcy_[CV_MAX_BLOBS];
	int pids_[CV_MAX_BLOBS];
	int page_[CV_MAX_BLOBS];
	long long pbornUs_[CV_MAX_BLOBS];
	bool taken_[CV_MAX_BLOBS];
	int pn_;

	// o carimbo do frame em curso, posto pela sonda; <= 0 e desarmado
	long long syncUs_;

	int nextId_;
	int matched_;
	int born_;

	bool churnAtivo_;
	CvChurnProbe churn_;
	float match_;
};
