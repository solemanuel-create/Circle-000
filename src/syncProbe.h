#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>

// ==================== sonda de sincronia (CV0_SYNC) ====================
// A peca tem tres relogios a bater em ritmos diferentes e ate hoje nao existia
// um unico sitio que os somasse:
//
//   o sensor entrega um frame quando quer (30,6 fps medidos no Kinect, e o
//         bloqueio do grab e que manda no ciclo);
//   a janela troca-buffer a outro ritmo (o periodo OF nunca foi definido nem
//         medido por codigo nenhum daqui, e com vsync ligado e o monitor a
//         mandar nele);
//   o motorista de audio e chamado a um terceiro (512 amostras a 48 kHz =
//         10,7 ms) e le os atomicos UMA vez por bloco, nao uma vez por frame.
//
// O que ele pediu como prioridade maxima e a sincronia entre som e imagem, e a
// pergunta nao e "quanto demora a sintetizar" - e "quanto tempo passa entre o
// corpo aparecer na sala e a imagem mudar, e entre a imagem mudar e o som
// mudar". Esses tres numeros nao existem em lado nenhum. Sem eles as quatro
// correccoes possiveis (nota no frame do nascimento, buffer mais curto,
// publicacao com marca de tempo, mascara como textura) sao escolhas as cegas,
// e a casa nao escolhe por adjetivo.
//
// Esta sonda so mede. Nao muda um numero de comportamento nenhum: nao toca em
// envelopes, nao toca em idades de gatilho, nao toca em ganhos, nao muda o
// caminho do pixel nem o do som. O que ela faz e carimbar instantes numa
// cadeia que ja existe e contar quanto tempo vao de um carimbo ao seguinte.
//
//   canal               de onde a ate onde                    em maos de quem
//   CAM_TELA  pixels do sensor recebidos  fim do draw         main
//   SWAP      fim do draw                 fim do draw seguinte main
//   CAM_PUB   pixels do sensor recebidos  publicacao no motor  main
//   PUB_CB    publicacao no motor         bloco que a le       motorista
//   NASCE_CB  nascimento do corpo         bloco que o toca     motorista
//   NASCE_PUB nascimento do corpo         publicacao da nota   main
//   IDADE     inicio do ciclo de entrega  entrega deste frame  main
//
// Os tres pedidos sao compostos por cima destes:
//
//   camara->tela   = CAM_TELA (limite INFERIOR: o que se ve ao centesimo)
//                    e CAM_TELA + SWAP (limite SUPERIOR: o buffer seguinte e o
//                    que vai para o monitor). Entre os dois nao ha um numero,
//                    ha um intervalo, e la-lo como ponto seria inventar.
//   camara->som    = CAM_PUB + PUB_CB + o que o driver poe depois do callback
//                    (esse ultimo nao e nosso nem medido daqui: uma janela de
//                    saida ASIO4ALL sobre o endpoint nao e legivel sem
//                    perguntar ao proprio driver, e o buffer fica declarado
//                    como cota minima).
//   nasce->nota    = NASCE_CB, que e o unico dos tres medido de ponta a ponta
//                    sem soma: o carimbo do nascimento viaja dentro do Evento
//                    ate ao bloco que o toca.
//
// O canal IDADE existe porque os seis acima comecam todos no instante em que
// `cap.read()` voltou, e esse instante nao e o instante do pixel: entre o
// sensor expor e a chamada nos devolver ja la vao a transferencia e a fila do
// backend. Era para esta linha sair de `cv::CAP_PROP_FRAME_TIMESTAMP` e nao
// sai, por um motivo que tem de ficar escrito em vez de ser descoberto por
// quem chegar depois: essa propriedade nao existe nesta build. O enum de
// `videoio.hpp` do OpenCV 4.11 aqui instalado vai de 0 a 72 e nao tem nenhuma
// marca de tempo por frame; as duas candidatas que tem, `CAP_PROP_PTS` (71) e
// `CAP_PROP_STREAM_OPEN_TIME_USEC` (55), dizem no proprio header que sao do
// backend FFmpeg, e FFmpeg nao esta nesta maquina. Chamar `cap.get()` com um
// numero inventado nao daria um erro, daria outra coisa: 30 e ISO_SPEED e 29 e
// GUID, e uma propriedade que nao existe devolve um numero que existe.
//
// Entao o que se mede e o que o ritmo de entrega obrig a admitir, e isso tem
// de ser dito como o que e: um LIMITE SUPERIOR da idade, nao uma marca do
// driver. Numa fila FIFO, uma leitura que bloqueia prova que a fila estava
// vazia quando comecou, portanto o frame que ela devolveu foi produzido
// depois desse inicio, e a sua idade e no maximo o tempo que a chamada esperou.
// As leituras seguintes que nao bloquearam nada tinham na frente o mesmo
// marco de inicio - a fila que elas vao esvaziando foi toda produzida depois
// dele - e a idade de cada uma e no maximo `agora - inicio do ciclo`. Quando a
// leitura seguinte volta a bloquear, o ciclo fecha-se e um novo comeca. O que
// a linha acusa em `rajada` e o que este canal nao e: o numero de frames que o
// backend entregou de uma so vez, que e a assinatura de agrupamento que os
// percentis do SWAP ja mostravam e que aqui ganha nome e tamanho.
//
// Duas cotas ficam declaradas porque sao mesmo cotas e nao numeros: a) o limite
// nao cobre o frame ainda em voo na transferencia USB, que para o backend ja
// esta entregue; b) sem o carimbo do driver nao ha como saber qual dos frames
// de uma rajada e o mais velho, so que nenhum e mais velho que o ciclo. E por
// isso que a camera->tela passa a sair em tres partes: a idade possivel do
// pixel, o que a peca faz com ele, e a soma dos dois como o numero que o
// orcamento de 120 ms pede.
//
// O relogio e um so, e nao e o do openFrameworks. `ofGetElapsedTimeMicros` e,
// por definicao, o tempo do processo, e so a main thread o pode chamar em
// seguranca; o callback do motorista nao chama OF nenhum, que e a primeira
// regra do modulo. Tudo na sonda vive portanto em `std::chrono::steady_clock`,
// e e por isso que um carimbo posto pela main e um lido pelo motorista se podem
// subtrair um ao outro sem nada cruzar.
//
// As amostras vivem em aneis fixos de deci-milisegundos, UM produtor por
// anel, e nunca sao limpas a meio de um run: limpar-los enquanto o motorista
// escreve seria partir a propria medicao. O que a linha de saida mostra como
// `n` e o total desde que a sonda foi armada, e o delta e a janela de cinco
// segundos - os percentis de uma corrida de 30 s sao o acumulado, e isso vai
// escrito na linha para nao haver leitura falsa.
//
// Um valor que nao caiba no deci-ms (acima de 6553 ms) ou que saia negativo
// (um relogio que retrocedeu) nao e cortado nem arredondado: entra em `fora`
// e e declarado. Um anel cheio tambem se acusa, em vez de dar voltas por cima
// de uma amostra que ainda nao foi lida.

enum CvSyncCanal {

	CSYNC_CAM_TELA = 0,
	CSYNC_SWAP,
	CSYNC_CAM_PUB,
	CSYNC_PUB_CB,
	CSYNC_NASCE_CB,
	CSYNC_NASCE_PUB,
	CSYNC_IDADE,

	CSYNC_N
};

class CvSyncProbe {

public:

	// 8192 deci-ms: a 94 Hz de callbacks sao 87 s de PUB_CB, e a corrida
	// autorizada e de 30 s. Encher antes disso e possivel e por isso e contado.
	static const int CAP = 8192;

	CvSyncProbe();

	// Armar e desarmar. Desarmado, cada um dos metodos abaixo sai pela primeira
	// linha e nao escreve absolutamente nada - e assim que uma medicao nova pode
	// viver no fonte sem mudar numero nenhum em cena.
	void setAtivo(bool a);
	bool ativo() const { return ativo_; }

	// O relogio da casa. `agora` e para quem nao tem um time_point na mao; `de`
	// e para quem ja o mediu (o callback do motor de sintese ja chama
	// steady_clock::now para o custo do bloco, e nao se vai chamar outro).
	static long long agora();
	static long long de(const std::chrono::steady_clock::time_point& t);

	// ----- main thread -----
	// Um frame do sensor chegou. `t` e o instante logo a seguir ao grab bloqueante.
	void frameChega(long long t);

	// Uma chamada de captura acabou, e `t0`/`t1` sao os dois instantes dela no
	// mesmo relogio da sonda. So se chama na rota do cv::VideoCapture: o 1473 nao
	// passa por la, e onde nao ha fila de backend nao ha idade para acusar.
	void leituraFeita(long long t0, long long t1);

	// O motor acabou de publicar os atomicos deste frame.
	void pubFim(long long t);
	// O draw deste frame acabou. O swap vem depois disto e nao e nosso.
	void drawFim(long long t);

	// ----- thread do motorista -----
	// Um bloco comecou: a idade dos dados que ele vai ler.
	void cbFim(long long t);
	// Uma nota nasceu deste nascimento e saiu neste instante.
	void nota(long long nasceUs, long long t);

	// ----- main thread, dentro do publish -----
	// O nascimento acabou de virar um Evento da fila. E o mesmo numero do
	// `nota` menos o que o motorista demora a ser chamado: separa o gatilho
	// (quantos frames de vida) do transporte (quantos blocos).
	void notaPublicada(long long nasceUs, long long t);

	// ----- saida -----
	// Os percentis sao a parte cara e acontecem UMA vez por janela, aqui, e nao
	// no HUD: o `churn` ja fez o mesmo (a nota e escrita na janela e o HUD le a
	// nota), e uma medicao que aloca ou ordena no caminho do desenho engole a si
	// mesma. Devolve o custo da propria saida em ms, para que a medicao se acuse
	// em vez de aparecer limpa.
	double resumo(long long agoraJanela);
	const char* tickNota() const { return tick_; }
	const char* hudNota() const { return hud_; }

	// O vsync como ele foi PEDIDO. Nao ha leitura de volta possivel: o
	// openFrameworks so expoe o setter (ofAppRunner.h:107, que no GLFW vai
	// direitinho para glfwSwapInterval em ofAppGLFWWindow.cpp:1664) e o estado
	// nao fica em lado nenhum que uma process possa consultar. E por isso que
	// este canal nao e a prova de nada: a prova de que o pedido bateu e a
	// distribuicao do SWAP, que e medida, e e ela que diz se quem mandou no
	// ritmo da janela foi o monitor ou a camera.
	bool vsyncPedido() const { return vsync_; }
	void setVsync(bool pedido) { vsync_ = pedido; }

	// A idade a que um corpo pulsa, para a linha decomposta nao dizer "3"
	// quando a corrida foi armada com CV0_MIDIAGE noutro numero.
	void setPulseAge(int n) { idadePulso_ = n; }

private:

	struct Anel {
		std::atomic<long> n;
		long fora;
		unsigned short v[CAP];

		Anel() : fora(0) { n.store(0); }

		// um produtor por anel: o indice e lido e avancado pelo mesmo thread, e
		// o `n` atomico e a unica coisa que o leitor ve
		void escreve(long long micros) {
			if (micros < 0) { fora++; return; }
			const long long d = micros / 100;
			if (d > 65535LL) { fora++; return; }
			const long i = n.load();
			if (i >= CAP) { fora++; return; }
			v[i] = (unsigned short)d;
			n.store(i + 1);
		}
	};

	// o resumo de um anel, ja em ms, calculado no `resumo` e lido pelo HUD. O
	// `fora` e do anel, nao do resumo: e o numero das amostras que nao couberam
	// na escala ou no deposito, e uma medicao que as deixasse cair em silencio
	// diria p90 de um conjunto que nao e o que foi medido.
	struct Estat {
		long n;
		long fora;
		double min, med, p90, max;
		Estat() : n(0), fora(0), min(0.0), med(0.0), p90(0.0), max(0.0) {}
	};

	void estat(int c, Estat& out);

	Anel anel_[CSYNC_N];

	bool ativo_;
	long long frameUs_;
	// o fim do draw anterior, para o periodo de troca. Vive no objeto e nao numa
	// static de funcao porque uma static destas nao se limpa quando a sonda e
	// armada, e o primeiro swap de um run seria o intervalo desde o ultimo draw
	// de um run anterior.
	long long drawUs_;
	bool temFrame_;
	// publicado pela main e lido pela thread do motorista: um atomico porque sao
	// duas maos, e um long long porque um corte a meio de uma marca de tempo
	// seria uma idade de seculo
	std::atomic<long long> pubUs_;

	// ----- o ciclo de entrega -----
	// Uma leitura que bloqueia abre o ciclo e a seguinte que bloqueia fecha-o:
	// entre as duas, tudo o que o backend deu ja foi produzido depois do inicio
	// da primeira. E por isso que o marco vive aqui dentro e nao em ofApp: o que
	// lhe da nome e uma propriedade da fila, e quem o poe tem de ser quem o le.
	// Nada disto e um numero de comportamento - sao contadores de uma coisa que
	// ja acontecia.
	static const long long ESPERA_UMBRAL = 2000;
	static const int HIST_N = 9;

	long long cicloT0_;
	int  rajada_;
	long nLeituras_;
	long nEspera_;
	long nRajadas_;
	long rajadaMax_;
	long hist_[HIST_N];

	long nFonte_;
	bool vsync_;
	int idadePulso_;

	Estat est_[CSYNC_N];
	long nAntes_[CSYNC_N];
	long long janelaT0_;
	double janelaS_;

	// as duas notas, escritas uma vez por janela e lidas em cada frame. O `tick_`
	// cresceu com o setimo canal e com a linha da rajada: os sete canais com
	// delta sao ~520 caracteres, e a composta com a idade sao ~420.
	char tick_[2048];
	char hud_[256];

	// o deposito para o nth_element: 16 KB que nao vivem na stack de ninguem e
	// que o processo nao perde a meio de uma corrida
	std::unique_ptr<unsigned short[]> esc_;
};
