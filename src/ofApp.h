#pragma once

#include "ofMain.h"
#include "ofxOpenCv.h"
#include "kinect1473.h"

#include "cvModel.h"
#include "cvSpace.h"
#include "blobModel.h"
#include "maskPipeline.h"
#include "paramTable.h"
#include "sourceTable.h"
#include "effectTable.h"
#include "midiOut.h"
#include "audioProbe.h"
#include "midiProbe.h"

#include <atomic>
#include <thread>

class ofApp : public ofBaseApp {

public:
	void setup();
	void update();
	void draw();
	// join da thread de ligacao antes de o ofApp deixar de existir
	void exit();

	void keyPressed(int key);
	void mouseMoved(int x, int y);
	void windowResized(int w, int h);

	void addRect(float x, float y, float w, float h);
	void reseed();

	ofImage img;
	ofFbo fbo;
	ofShader fx;
	CvParams params;

	// Os tres espacos por onde um pixel anda (fonte / maquina / janela) e a
	// maquina de silhueta em si. O ofApp nao segura mais nenhum buffer de
	// rastreio: ele entrega o frame e le os contornos que voltam.
	CvSpace space;
	MaskPipeline mask;

	// P2: a faixa, o passo e a tecla de cada parametro vivem aqui e em mais
	// nenhum sitio. O keyPressed nomeia a linha por id; o HUD desenha a linha
	// inteira. Os dois leem a mesma tabela, por isso nao ha como discordarem.
	ParamTable table;

	// P3: o mesmo principio para as duas escolhas que nao sao numeros. O que uma
	// fonte e (nome, tecla, token de ambiente, o que se procura na lista do
	// backend, se o c e o v tem o que fazer aqui) mora numa linha; o que um
	// efeito e (nome, tecla, o inteiro do uEffect) mora noutra. O ofApp passou a
	// perguntar as tabelas em vez de comparar ids soltos.
	SourceTable sources;
	EffectTable effects;

	// o resultado da leitura do frag contra a tabela de efeitos: 0 bate tudo,
	// 1 o frag nao foi lido (nao ha como saber), 2 diverge. Sao tres estados
	// porque "nao verificado" e uma resposta diferente de "verificado e errado",
	// e em cena as duas cores significam acoes diferentes.
	int fxContract = 0;
	string fxContractNote;

	ofVec2f mouse;

	vector<ofRectangle> rects;
	vector<ofVec2f> dir;

	// P4: o que vai para o uBlobs e o que o overlay desenha vivem no mesmo slot
	// de um registo de arrays fixos, com um id que sobrevive de um frame para o
	// outro. Nao ha mais nenhum vector no caminho do frame: nem o float[128] com
	// indexacao magica, nem o idx(n) que a ordenacao por area construia a cada
	// actualizacao, nem o scaled que crescia frame a frame.
	BlobRegistry blobs;
	vector<int> blobOrder;

	// o #define CV_MAX_BLOBS do frag contra o CV_MAX_BLOBS do C++, lido uma vez
	// no setup com o mesmo tres estados do contrato de efeitos
	int blobContract = 0;
	string blobContractNote;

	// O HUD desenha uma linha de cada vez e o texto nasce em buffers fixos: as
	// cadeias de operator+ eram tres alocacoes por linha, por frame. O que ainda
	// sobra e a copia que o propio ofDrawBitmapString faz la dentro, e essa nao
	// e nossa.
	char hudTxt_[256];
	string hudLinha_;
	string hudPeda_;

	bool shaderOk = false;
	int activeBlobs = 0;

	void drawOverlay();

	// As pecas do HUD sem alocacao. hudFmt escreve na hudTxt_ e devolve o
	// comprimento do que coube (o _TRUNCATE e o que impede o abort do sprintf_s
	// num buffer curto, e a sonda de igualdade e o que prova que aqui nada e
	// cortado). hudDraw e hudWrap desenham por prefixo mais recorte: os dois
	// strings de destino tem capacidade reservada no setup, por isso nao ha
	// copia de linha nem substr de segmento. hudWrap e a coluna de 66 caracteres
	// e quebra no ultimo espaco, nao no meio de um numero.
	size_t hudFmt(const char* fmt, ...);
	void hudDraw(int& y, const char* prefix, const char* s, size_t n);
	void hudWrap(int& y, const char* prefix, const char* s, size_t n);

	// Sonda do sistema de log do openFrameworks, atras de variavel de ambiente.
	// So existe para responder uma pergunta que hoje esta no escuro: o silencio
	// imposto em main.cpp e anterior ao core restaurado, e enquanto ele vale os
	// addons nao logam nada - e o log dos addons e exatamente o que diagnosticaria
	// uma dependencia nova. Sem a variavel, a funcao nao faz absolutamente nada.
	void logProbe();

	// Sonda de procedencia do nascimento (CV0_CHURN), atras da mesma variavel.
	// De cinco em cinco segundos tira a diferenca dos contadores do registo para
	// o stderr e para uma linha do HUD. Sem a variavel o `if` de cima nem a
	// chama: nenhum campo do registo e lido, nenhum contador se move.
	void churnDump();

	// ----- camera -----
	// O videoInput do openFrameworks morre ao abrir o dispositivo nesta
	// instalacao (SIGSEGV em listDevices e em setup). O cv::VideoCapture le
	// frames reais, entao a entrada passa por ele e os nomes vem das duas
	// enumeracoes proprias que os backends usam por baixo (cvDevices.cpp).
	cv::VideoCapture cap;
	cv::Mat mFrame, mRgb;
	ofTexture texLive;
	// as duas listas vivem lado a lado porque sao duas enumeracoes diferentes do
	// mesmo hardware: DSHOW e MSMF numeram cada uma do seu jeito e um indice
	// so vale dentro da lista do backend com que ele e usado
	vector<string> camNames;
	vector<string> camNamesMf;
	bool capOk = false;
	bool texOk = false;
	bool frameNew = false;
	int capIndex = -1;
	int capApi = 0;
	// qual fonte esta de pe de verdade, nao qual foi pedida: e o que diz se a
	// proxima tecla de fonte tem algo a mudar ou se so custaria um buraco no
	// palco. -1 enquanto nada abriu.
	int srcAtivo = -1;
	string capName;
	string capNote;
	// aviso de uma tecla: vive ate a proxima, para o 'p' poder dizer que o
	// backend atual nao tem dialog
	string keyNote;

	// ----- Kinect 360 modelo 1473 -----
	// Cor vai para a mesma textura da webcam; profundidade vai para a mesma
	// maquina de contornos. So a origem dos pixels muda.
	Kinect1473 kinect;
	bool kinectOk = false;
	cv::Mat kColor, kDepth;

	// O NuiInitialize frio leva 20-25 s porque e o Kinect Service que acorda,
	// nao o nosso codigo. Na main thread isso e uma janela preta e sem tecla,
	// em cena indistinguivel de um crash. Entao a ligacao vai para uma thread e
	// o HUD mostra o tempo ate ela voltar.
	std::thread kinectThread;
	std::atomic<bool> kinectWaking{ false };
	std::atomic<bool> kinectOpenDone{ false };
	bool kinectOpenResult = false;
	string kinectOpenNote;
	double kinectWakeStart = 0.0;
	// quantas ligacoes o processo ja pagou: e a unica prova em log de que a
	// tecla repetida nao derrubou um sensor saudavel para ligar o mesmo de novo
	int kinectOpens = 0;

	// ----- pacote 4: barramento de sinais, envelope, mapa de ganhos -----
	// Os alvos crus vivem dentro de sig; os numeros abaixo sao o resultado do
	// envelope vezes o ganho, e sao a unica coisa que o render le. O som le
	// sig, nao isto aqui, para que os dois tenham a mesma hora sem um depender
	// do outro.
	CvSignals sig;
	float rExposure = 0.0f;
	float rGrow = 0.0f;
	float rThresh = 0.0f;
	float rDither = 0.0f;
	float rSpeed = 0.0f;
	ofVec2f prevCent = ofVec2f(0.0f, 0.0f);
	bool haveCent = false;
	// o custo do barramento tem que aparecer no HUD: se ele passar do
	// orcamento do rastreio, a reacao passou a custar fps
	double sigMs = 0.0;

	// Pacote 5, passo 1: o resultado da sonda de transporte. O relatorio fica
	// vivo porque o HUD o le em cada frame, e porque a unica pergunta que ele
	// responde - que rota de audio abre nesta maquina - nao tem resposta sem
	// CV0_AUDIOPROBE. Sem a variavel a struct e so um bool false.
	AudioProbeReport audioProbe;

	// Pacote 5, rota de saida por MIDI: o resultado do laco objetivo (mandar
	// pela ponta OUT e contar o que volta pela ponta IN do mesmo cabo). Mesma
	// porta de ambiente da sonda de transporte, variavel propria.
	MidiProbeReport midiProbe;

	// Pacote 5, o cabo vivo: o barramento sai por aqui e vai para o TouchDesigner
	// ou para qualquer app atras da porta loopMIDI. Nao e sonda nem opcional -
	// abre em todo inicio e escolhe a saida por nome. O custo vai para midiMs pelo
	// mesmo motivo do sigMs: se um dia ele comer o orcamento do frame, e na tela
	// que isso se ve, nao depois.
	MidiOut midiOut;
	double midiMs = 0.0;

	// o master ja decidido, lido pelo cabo em vez de recontado la dentro: o
	// render e o MIDI tem que mandar exatamente o mesmo numero no mesmo frame
	float rMaster = 0.0f;

	// ----- sonda de churn (CV0_CHURN) -----
	// A fotografia anterior dos contadores do registo e o que permite publicar a
	// diferenca: taxa por janela, nao acumulado desde o inicio do run. A nota e
	// um array de stack do proprio objeto, sem string, porque o HUD a le em
	// cada frame e uma medicao que aloca no caminho do desenho engole a si
	// mesma. Sem a variavel nenhum destes campos e escrito.
	bool churnAtiva = false;
	double churnProx = 0.0;
	double churnUltima = 0.0;
	CvChurnProbe churnAntes = {};
	char churnNota[512] = { 0 };

	bool camStalled = false;
	// As duas origens do frame, calculadas uma vez no update() e lidas por nome
	// em vez de cada leitor re-inventar o que `liveSource` queria dizer.
	CvFrameOrigin origin;
	double lastNewFrame = 0.0;

	// Onde vai o orcamento do frame decide o que corrigir: se a espera pela
	// camera manda, e o modo do sensor (resolucao/exposicao); se o rastreio
	// manda, e a resolucao da maquina. Os tres numeros vao para o HUD.
	double readMs = 0.0;
	double trackMs = 0.0;
	double cycleMs = 0.0;

	// o indice sai sempre da lista do backend que vai ser aberto: a busca e
	// feita em camNames ou camNamesMf conforme o caso, nunca na outra
	int pickIndex(const vector<string>& list, const string& needle);
	void chooseSourceFromEnv();
	// forcar e o "sei que vou pagar a ligacao de novo": usado por quem muda
	// algo no pedido (backend, modo do sensor). Sem ele, ligar o que ja esta
	// ligado e um nao-operao com aviso no HUD.
	bool openSource(bool forcar = false);
	void closeSource();
	bool openIndex(int idx, int api);
	void updateSignals(float dt);
	void applyGains();
	void uploadTexture();
	void pushCameraBlobs();
	void pushSyntheticBlobs(float dt);
	// As duas perguntas tem duas funcoes. `drawSource()` e o que vai para o fbo
	// e, porque o espaco do shader e o espaco do que se desenha, e ela tambem
	// que da o tamanho ao CvSpace. `stillSource()` e a foto: o fallback dos
	// pixels e o espaco onde o baralho de retangulos e semeado - amarrar os
	// dois a esta unica funcao e o que impede o baralho de nascer num espaco
	// que a tela nao esta mostrando.
	ofBaseDraws* drawSource();
	ofBaseDraws* stillSource();
};
